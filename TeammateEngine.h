#include <iostream>
#include <vector>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <random>
#include <bitset>
#include <thread>
#include <mutex>
#include <atomic>

using namespace std;
using namespace std::chrono;

const int SIZE = 7;
const int CELL_COUNT = 49;
const uint64_t FULL_BOARD_MASK = (1ULL << 49) - 1;

const int PLAYER1 = 1;
const int PLAYER2 = 2;

const int INF = 1000000;
const int TT_SIZE = 1048576;
const double TIME_LIMIT_SECONDS = 2.0;
const int THREAD_COUNT = 4;

inline int popcount64(uint64_t x) {
    return (int)bitset<64>(x).count();
}

inline int ctz64(uint64_t x) {
    int count = 0;
    while ((x & 1ULL) == 0) {
        x >>= 1;
        count++;
    }
    return count;
}

uint64_t ADJ_MASK[49];
uint64_t JUMP_MASK[49];
uint64_t ZOBRIST[49][2];
uint64_t ZOBRIST_TURN;

struct Move {
    int from;
    int to;
    bool isClone;
    int infectCount;
    int orderScore;

    bool operator<(const Move& other) const {
        return orderScore > other.orderScore;
    }
};

struct Bitboard {
    uint64_t p1 = 0;
    uint64_t p2 = 0;
    uint64_t hashKey = 0;

    void init() {
        p1 = 0;
        p2 = 0;
        hashKey = 0;

        p1 |= (1ULL << 0);
        p1 |= (1ULL << 48);

        p2 |= (1ULL << 6);
        p2 |= (1ULL << 42);

        hashKey ^= ZOBRIST[0][0];
        hashKey ^= ZOBRIST[48][0];
        hashKey ^= ZOBRIST[6][1];
        hashKey ^= ZOBRIST[42][1];
        hashKey ^= ZOBRIST_TURN;
    }

    bool isGameOver() const {
        uint64_t empty = ~(p1 | p2) & FULL_BOARD_MASK;
        return empty == 0 || p1 == 0 || p2 == 0;
    }
};

enum TTFlag {
    EXACT,
    LOWERBOUND,
    UPPERBOUND
};

struct TTEntry {
    uint64_t hash = 0;
    int depth = -1;
    int value = 0;
    TTFlag flag = EXACT;
};

vector<TTEntry> TT(TT_SIZE);
mutex ttMutex;

class GPTAI {
public:
    static atomic<long long> totalNodes;
    static atomic<long long> ttHits;
    static atomic<long long> ttLookups;

    static time_point<steady_clock> searchStartTime;
    static atomic<bool> timeOver;
    static int reachedDepth;

    static void checkTime() {
        double elapsed =
            duration<double>(steady_clock::now() - searchStartTime).count();

        if (elapsed >= TIME_LIMIT_SECONDS) {
            timeOver = true;
        }
    }

    static int opponent(int color) {
        return color == PLAYER1 ? PLAYER2 : PLAYER1;
    }

    static int mobilityScore(const Bitboard& b, int color) {
        uint64_t my = color == PLAYER1 ? b.p1 : b.p2;
        uint64_t empty = ~(b.p1 | b.p2) & FULL_BOARD_MASK;

        int score = 0;
        uint64_t temp = my;

        while (temp) {
            int idx = ctz64(temp);
            score += popcount64((ADJ_MASK[idx] | JUMP_MASK[idx]) & empty);
            temp &= temp - 1;
        }

        return score;
    }

    static int densityScore(uint64_t stones) {
        int score = 0;
        uint64_t temp = stones;

        while (temp) {
            int idx = ctz64(temp);
            score += popcount64(ADJ_MASK[idx] & stones);
            temp &= temp - 1;
        }

        return score;
    }

    static int evaluate(const Bitboard& b, int myColor) {
        uint64_t my = myColor == PLAYER1 ? b.p1 : b.p2;
        uint64_t opp = myColor == PLAYER1 ? b.p2 : b.p1;

        int myCount = popcount64(my);
        int oppCount = popcount64(opp);
        int total = myCount + oppCount;

        if (oppCount == 0) return INF;
        if (myCount == 0) return -INF;
        if (total == 0) return 0;

        double myPercent = ((double)myCount / total) * 100.0;

        int pieceScore = myCount - oppCount;

        int mobility = mobilityScore(b, myColor);
        int oppMobility = mobilityScore(b, opponent(myColor));
        int mobilityDiff = mobility - oppMobility;

        int density = densityScore(my);
        int oppDensity = densityScore(opp);
        int densityDiff = density - oppDensity;

        if (myPercent < 45.0) {
            return pieceScore * 10 + densityDiff * 70 + mobilityDiff * 20;
        }
        else if (myPercent <= 55.0) {
            return pieceScore * 20 + mobilityDiff * 90 + densityDiff * 10;
        }
        else {
            return pieceScore * 120 + mobilityDiff * 20;
        }
    }

    static int minimax(
        const Bitboard& b,
        int depth,
        int alpha,
        int beta,
        bool isMax,
        int myColor
    ) {
        totalNodes++;

        if ((totalNodes.load() & 2047) == 0) {
            checkTime();

            if (timeOver) {
                return evaluate(b, myColor);
            }
        }

        int alphaOrig = alpha;
        int betaOrig = beta;

        int ttIndex = b.hashKey & (TT_SIZE - 1);

        {
            lock_guard<mutex> lock(ttMutex);

            TTEntry& tte = TT[ttIndex];

            ttLookups++;

            if (tte.hash == b.hashKey && tte.depth >= depth) {
                ttHits++;

                if (tte.flag == EXACT)
                    return tte.value;

                if (tte.flag == LOWERBOUND)
                    alpha = max(alpha, tte.value);

                if (tte.flag == UPPERBOUND)
                    beta = min(beta, tte.value);

                if (alpha >= beta)
                    return tte.value;
            }
        }

        if (depth == 0 || b.isGameOver()) {
            return evaluate(b, myColor);
        }

        int currColor = isMax ? myColor : opponent(myColor);
        vector<Move> moves = getValidMoves(b, currColor);

        if (moves.empty()) {
            return evaluate(b, myColor);
        }

        int best = isMax ? -INF : INF;

        for (auto& m : moves) {
            if (timeOver)
                break;

            Bitboard nextB = applyMove(b, m, currColor);

            int val = minimax(
                nextB,
                depth - 1,
                alpha,
                beta,
                !isMax,
                myColor
            );

            if (isMax) {
                best = max(best, val);
                alpha = max(alpha, best);
            }
            else {
                best = min(best, val);
                beta = min(beta, best);
            }

            if (beta <= alpha)
                break;
        }

        if (!timeOver) {
            lock_guard<mutex> lock(ttMutex);

            TTEntry& tte = TT[ttIndex];

            tte.hash = b.hashKey;
            tte.depth = depth;
            tte.value = best;

            if (best <= alphaOrig)
                tte.flag = UPPERBOUND;
            else if (best >= betaOrig)
                tte.flag = LOWERBOUND;
            else
                tte.flag = EXACT;
        }

        return best;
    }

    static vector<Move> getValidMoves(const Bitboard& b, int color) {
        vector<Move> moves;

        uint64_t my = color == PLAYER1 ? b.p1 : b.p2;
        uint64_t opp = color == PLAYER1 ? b.p2 : b.p1;
        uint64_t empty = ~(b.p1 | b.p2) & FULL_BOARD_MASK;

        uint64_t temp = my;

        while (temp) {
            int from = ctz64(temp);
            uint64_t clones = ADJ_MASK[from] & empty;

            while (clones) {
                int to = ctz64(clones);
                int caps = popcount64(ADJ_MASK[to] & opp);

                moves.push_back({
                    from,
                    to,
                    true,
                    caps,
                    caps * 100 + 10
                    });

                clones &= clones - 1;
            }

            temp &= temp - 1;
        }

        temp = my;

        while (temp) {
            int from = ctz64(temp);
            uint64_t jumps = JUMP_MASK[from] & empty;

            while (jumps) {
                int to = ctz64(jumps);
                int caps = popcount64(ADJ_MASK[to] & opp);

                moves.push_back({
                    from,
                    to,
                    false,
                    caps,
                    caps * 100
                    });

                jumps &= jumps - 1;
            }

            temp &= temp - 1;
        }

        sort(moves.begin(), moves.end());
        return moves;
    }

    static Bitboard applyMove(Bitboard b, Move m, int color) {
        uint64_t* my = color == PLAYER1 ? &b.p1 : &b.p2;
        uint64_t* opp = color == PLAYER1 ? &b.p2 : &b.p1;

        int myIdx = color == PLAYER1 ? 0 : 1;
        int oppIdx = color == PLAYER1 ? 1 : 0;

        if (!m.isClone) {
            *my &= ~(1ULL << m.from);
            b.hashKey ^= ZOBRIST[m.from][myIdx];
        }

        *my |= (1ULL << m.to);
        b.hashKey ^= ZOBRIST[m.to][myIdx];

        uint64_t captured = ADJ_MASK[m.to] & (*opp);

        *my |= captured;
        *opp &= ~captured;

        uint64_t temp = captured;

        while (temp) {
            int idx = ctz64(temp);

            b.hashKey ^= ZOBRIST[idx][oppIdx];
            b.hashKey ^= ZOBRIST[idx][myIdx];

            temp &= temp - 1;
        }

        b.hashKey ^= ZOBRIST_TURN;

        return b;
    }

    static Move getBestMove(const Bitboard& board, int myColor) {
        searchStartTime = steady_clock::now();
        timeOver = false;
        reachedDepth = 0;

        vector<Move> moves = getValidMoves(board, myColor);

        if (moves.empty()) {
            return { -1, -1, false, 0, 0 };
        }

        Move globalBestMove = moves[0];

        for (int currentDepth = 1; currentDepth <= 64; currentDepth++) {
            if (timeOver)
                break;

            Move depthBestMove = globalBestMove;
            int bestScore = -INF;

            auto it = find_if(
                moves.begin(),
                moves.end(),
                [&](const Move& m) {
                    return m.from == globalBestMove.from &&
                        m.to == globalBestMove.to &&
                        m.isClone == globalBestMove.isClone;
                }
            );

            if (it != moves.end() && it != moves.begin()) {
                iter_swap(moves.begin(), it);
            }

            mutex bestMutex;
            atomic<int> nextIndex(0);

            vector<thread> workers;

            for (int t = 0; t < THREAD_COUNT; t++) {
                workers.emplace_back([&]() {
                    while (true) {
                        int index = nextIndex.fetch_add(1);

                        if (index >= (int)moves.size() || timeOver)
                            break;

                        Move m = moves[index];
                        Bitboard nextB = applyMove(board, m, myColor);

                        int score = minimax(
                            nextB,
                            currentDepth - 1,
                            -INF,
                            INF,
                            false,
                            myColor
                        );

                        if (timeOver)
                            break;

                        lock_guard<mutex> lock(bestMutex);

                        if (score > bestScore) {
                            bestScore = score;
                            depthBestMove = m;
                        }
                    }
                    });
            }

            for (thread& th : workers) {
                if (th.joinable())
                    th.join();
            }

            if (!timeOver) {
                globalBestMove = depthBestMove;
                reachedDepth = currentDepth;
            }
        }

        return globalBestMove;
    }
};

atomic<long long> GPTAI::totalNodes = 0;
atomic<long long> GPTAI::ttHits = 0;
atomic<long long> GPTAI::ttLookups = 0;

time_point<steady_clock> GPTAI::searchStartTime;
atomic<bool> GPTAI::timeOver = false;
int GPTAI::reachedDepth = 0;

void initEngine() {
    mt19937_64 rng(12345);

    for (int i = 0; i < 49; i++) {
        ADJ_MASK[i] = 0;
        JUMP_MASK[i] = 0;

        ZOBRIST[i][0] = rng();
        ZOBRIST[i][1] = rng();

        int r = i / 7;
        int c = i % 7;

        for (int dr = -2; dr <= 2; dr++) {
            for (int dc = -2; dc <= 2; dc++) {
                if (dr == 0 && dc == 0)
                    continue;

                int nr = r + dr;
                int nc = c + dc;

                if (nr < 0 || nr >= 7 || nc < 0 || nc >= 7)
                    continue;

                int nidx = nr * 7 + nc;
                int dist = max(abs(dr), abs(dc));

                if (dist == 1) {
                    ADJ_MASK[i] |= (1ULL << nidx);
                }
                else if (dist == 2) {
                    JUMP_MASK[i] |= (1ULL << nidx);
                }
            }
        }
    }

    ZOBRIST_TURN = rng();
}

int main() {
    cout << "Program Start\n";

    initEngine();

    Bitboard board;
    board.init();

    int turn = PLAYER1;
    int turns = 0;

    while (!board.isGameOver() && turns < 200) {
        vector<Move> moves = GPTAI::getValidMoves(board, turn);

        if (!moves.empty()) {
            cout << "Turn " << turns + 1
                << " | Player " << turn
                << " thinking...\n";

            Move bestMove = GPTAI::getBestMove(board, turn);

            cout << "Reached Depth: " << GPTAI::reachedDepth << "\n";

            board = GPTAI::applyMove(board, bestMove, turn);
        }

        turn = (turn == PLAYER1) ? PLAYER2 : PLAYER1;
        turns++;
    }

    int p1 = popcount64(board.p1);
    int p2 = popcount64(board.p2);

    cout << "\n===== RESULT =====\n";
    cout << "PLAYER1: " << p1 << endl;
    cout << "PLAYER2: " << p2 << endl;

    if (p1 > p2)
        cout << "PLAYER1 WIN\n";
    else if (p2 > p1)
        cout << "PLAYER2 WIN\n";
    else
        cout << "DRAW\n";

    double hitRate =
        (GPTAI::ttLookups > 0)
        ? ((double)GPTAI::ttHits / GPTAI::ttLookups) * 100.0
        : 0.0;

    cout << "\nTT Hit Rate: " << hitRate << "%\n";
    cout << "Total Nodes: " << GPTAI::totalNodes << endl;

    system("pause");

    return 0;
}
