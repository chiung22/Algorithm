#pragma once
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

#ifdef _MSC_VER
#include <intrin.h>
#endif

using namespace std;
using namespace std::chrono;

namespace Teammate {

    inline int popcount64(uint64_t x) {
#ifdef _MSC_VER
        return (int)__popcnt64(x);
#else
        return (int)__builtin_popcountll(x);
#endif
    }

    inline int ctz64(uint64_t x) {
#ifdef _MSC_VER
        unsigned long index;
        _BitScanForward64(&index, x);
        return (int)index;
#else
        return (int)__builtin_ctzll(x);
#endif
    }

    const int SIZE = 7;
    const int CELL_COUNT = 49;
    const uint64_t FULL_BOARD_MASK = (1ULL << 49) - 1;

    const int PLAYER1 = 1;
    const int PLAYER2 = 2;

    const int INF = 1000000;
    const int TT_SIZE = 1048576;

    inline static uint64_t ADJ_MASK[49];
    inline static uint64_t JUMP_MASK[49];
    inline static uint64_t ZOBRIST_TABLE[49][2];
    inline static uint64_t ZOBRIST_TURN;

    enum TTFlag { EXACT, LOWERBOUND, UPPERBOUND };
    struct TTEntry { uint64_t hash = 0; int depth = -1; int flag = EXACT; int value = 0; };

    inline static vector<TTEntry> TT_Cache(TT_SIZE);
    inline static mutex ttMutex;

    // [치웅 엔진과 동일한 조건] 멀티스레드 제어용 원자적 변수 추가
    inline static atomic<bool> searchCancelled{ false };

    struct Move {
        int from, to; bool isClone; int infectCount; int orderScore;
        bool operator<(const Move& other) const { return orderScore > other.orderScore; }
        bool operator==(const Move& other) const { return from == other.from && to == other.to && isClone == other.isClone; }
    };

    struct Bitboard {
        uint64_t p1 = 0, p2 = 0, hashKey = 0;
        void init() {
            p1 = 0; p2 = 0; hashKey = 0;
            p1 |= (1ULL << 0); p1 |= (1ULL << 48);
            p2 |= (1ULL << 6); p2 |= (1ULL << 42);
            hashKey ^= ZOBRIST_TABLE[0][0]; hashKey ^= ZOBRIST_TABLE[48][0];
            hashKey ^= ZOBRIST_TABLE[6][1]; hashKey ^= ZOBRIST_TABLE[42][1];
            hashKey ^= ZOBRIST_TURN;
        }
        bool isGameOver() const {
            uint64_t empty = ~(p1 | p2) & FULL_BOARD_MASK;
            return (empty == 0 || p1 == 0 || p2 == 0);
        }
    };

    class AtaxxEngine {
    public:
        inline static atomic<long long> totalNodes{ 0 };
        inline static atomic<long long> ttHits{ 0 };
        inline static atomic<long long> ttLookups{ 0 };

        // [치웅 엔진과 동일한 조건] 최고 기록 저장소
        inline static Move bestMoveOverall;
        inline static int highestReachedDepth = 0;
        inline static mutex bestMoveMutex;

        static int evaluate(const Bitboard& b, int myColor, int strat_dis, int strat_even, int strat_adv) {
            uint64_t my = myColor == PLAYER1 ? b.p1 : b.p2;
            uint64_t opp = myColor == PLAYER1 ? b.p2 : b.p1;

            int myCount = popcount64(my); int oppCount = popcount64(opp);
            int total = myCount + oppCount;

            if (oppCount == 0) return INF;
            if (myCount == 0) return -INF;
            if (total == 0) return 0;

            double myPercent = ((double)myCount / total) * 100.0;
            int pieceScore = myCount - oppCount;
            int mobilityScore = 0, densityScore = 0;
            uint64_t temp = my; uint64_t empty = ~(b.p1 | b.p2) & FULL_BOARD_MASK;

            while (temp) {
                int idx = ctz64(temp);
                densityScore += popcount64(ADJ_MASK[idx] & my);
                mobilityScore += popcount64((ADJ_MASK[idx] | JUMP_MASK[idx]) & empty);
                temp &= temp - 1;
            }

            int currentStrategy = strat_even;
            if (myPercent < 45.0) currentStrategy = strat_dis;
            else if (myPercent > 55.0) currentStrategy = strat_adv;

            if (currentStrategy == 1) return pieceScore * 10 + densityScore * 60;
            else if (currentStrategy == 2) return pieceScore * 20 + mobilityScore * 80;
            else return pieceScore * 100;
        }

        static int minimax(const Bitboard& b, int depth, int alpha, int beta, bool isMax, int myColor, int strat_dis, int strat_even, int strat_adv) {
            // [치웅 엔진과 동일한 조건] 시간 종료 시 즉각 연산 중단
            if (searchCancelled) return 0;

            totalNodes.fetch_add(1, memory_order_relaxed);
            int alphaOrig = alpha, betaOrig = beta;
            int ttIndex = (int)(b.hashKey & (TT_SIZE - 1));

            {
                lock_guard<mutex> lock(ttMutex);
                TTEntry& tte = TT_Cache[ttIndex];
                ttLookups.fetch_add(1, memory_order_relaxed);

                if (tte.hash == b.hashKey && tte.depth >= depth) {
                    ttHits.fetch_add(1, memory_order_relaxed);
                    if (tte.flag == EXACT) return tte.value;
                    if (tte.flag == LOWERBOUND) alpha = max(alpha, tte.value);
                    if (tte.flag == UPPERBOUND) beta = min(beta, tte.value);
                    if (alpha >= beta) return tte.value;
                }
            }

            if (depth == 0 || b.isGameOver()) return evaluate(b, myColor, strat_dis, strat_even, strat_adv);

            int currColor = isMax ? myColor : (myColor == PLAYER1 ? PLAYER2 : PLAYER1);
            vector<Move> moves = getValidMoves(b, currColor);
            if (moves.empty()) return evaluate(b, myColor, strat_dis, strat_even, strat_adv);

            int best = isMax ? -INF : INF;

            for (auto& m : moves) {
                Bitboard nextB = applyMove(b, m, currColor);
                int val = minimax(nextB, depth - 1, alpha, beta, !isMax, myColor, strat_dis, strat_even, strat_adv);

                if (searchCancelled) return 0;

                if (isMax) { best = max(best, val); alpha = max(alpha, best); }
                else { best = min(best, val); beta = min(beta, best); }
                if (beta <= alpha) break;
            }

            if (!searchCancelled) {
                lock_guard<mutex> lock(ttMutex);
                TTEntry& tte = TT_Cache[ttIndex];
                tte.hash = b.hashKey; tte.depth = depth; tte.value = best;
                if (best <= alphaOrig) tte.flag = UPPERBOUND;
                else if (best >= betaOrig) tte.flag = LOWERBOUND;
                else tte.flag = EXACT;
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
                    moves.push_back({ from, to, true, caps, caps * 100 + 10 });
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
                    moves.push_back({ from, to, false, caps, caps * 100 });
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

            if (!m.isClone) { *my &= ~(1ULL << m.from); b.hashKey ^= ZOBRIST_TABLE[m.from][myIdx]; }
            *my |= (1ULL << m.to); b.hashKey ^= ZOBRIST_TABLE[m.to][myIdx];

            uint64_t captured = ADJ_MASK[m.to] & (*opp);
            *my |= captured; *opp &= ~captured;

            uint64_t temp = captured;
            while (temp) {
                int idx = ctz64(temp);
                b.hashKey ^= ZOBRIST_TABLE[idx][oppIdx]; b.hashKey ^= ZOBRIST_TABLE[idx][myIdx];
                temp &= temp - 1;
            }
            b.hashKey ^= ZOBRIST_TURN;
            return b;
        }

        // =========================================================================
        // 🔥 [치웅 엔진과 동일한 조건] Lazy SMP 멀티스레드 + 시간제한 + 무한 뎁스
        // =========================================================================
        static Move getBestMoveTimeLimited(const Bitboard& b, int myColor, double timeLimitSeconds, int strat_dis, int strat_even, int strat_adv) {
            searchCancelled = false;
            totalNodes = 0; ttHits = 0; ttLookups = 0; highestReachedDepth = 0;

            vector<Move> validMoves = getValidMoves(b, myColor);
            if (validMoves.empty()) return { -1, -1, false, 0, 0 };
            if (validMoves.size() == 1) return validMoves[0];

            bestMoveOverall = validMoves[0];

            int numThreads = (int)std::thread::hardware_concurrency();
            if (numThreads == 0) numThreads = 4;

            vector<thread> workers;

            for (int t = 0; t < numThreads; t++) {
                workers.emplace_back([&, t, validMoves, myColor, b, strat_dis, strat_even, strat_adv]() {
                    vector<Move> threadMoves = validMoves;

                    if (t > 0) {
                        mt19937 rng(12345 + t);
                        shuffle(threadMoves.begin() + 1, threadMoves.end(), rng);
                    }

                    Move localBestMove = threadMoves[0];

                    for (int depth = 1; depth <= 100; depth++) {
                        int maxEval = -INF, alpha = -INF, beta = INF;
                        Move depthBestMove = threadMoves[0];

                        auto it = find(threadMoves.begin(), threadMoves.end(), localBestMove);
                        if (it != threadMoves.end() && it != threadMoves.begin()) {
                            iter_swap(threadMoves.begin(), it);
                        }

                        for (const Move& m : threadMoves) {
                            Bitboard nextB = applyMove(b, m, myColor);
                            int eval = minimax(nextB, depth - 1, alpha, beta, false, myColor, strat_dis, strat_even, strat_adv);
                            if (searchCancelled) break;

                            if (eval > maxEval) { maxEval = eval; depthBestMove = m; }
                            alpha = max(alpha, eval);
                        }

                        if (searchCancelled) break;
                        localBestMove = depthBestMove;

                        lock_guard<mutex> lock(bestMoveMutex);
                        if (depth > highestReachedDepth || (t == 0 && depth == highestReachedDepth)) {
                            highestReachedDepth = depth;
                            bestMoveOverall = localBestMove;
                        }
                    }
                    });
            }

            auto start = high_resolution_clock::now();
            while (duration<double>(high_resolution_clock::now() - start).count() < timeLimitSeconds) {
                this_thread::sleep_for(milliseconds(5));
            }

            searchCancelled = true;

            for (auto& w : workers) {
                if (w.joinable()) w.join();
            }

            return bestMoveOverall;
        }
    };

    void initEngine() {
        mt19937_64 rng(12345);
        for (int i = 0; i < 49; i++) {
            ADJ_MASK[i] = 0; JUMP_MASK[i] = 0;
            ZOBRIST_TABLE[i][0] = rng(); ZOBRIST_TABLE[i][1] = rng();
            int r = i / 7; int c = i % 7;
            for (int dr = -2; dr <= 2; dr++) {
                for (int dc = -2; dc <= 2; dc++) {
                    if (dr == 0 && dc == 0) continue;
                    int nr = r + dr; int nc = c + dc;
                    if (nr < 0 || nr >= 7 || nc < 0 || nc >= 7) continue;
                    int nidx = nr * 7 + nc;
                    int dist = max(abs(dr), abs(dc));
                    if (dist == 1) ADJ_MASK[i] |= (1ULL << nidx);
                    else if (dist == 2) JUMP_MASK[i] |= (1ULL << nidx);
                }
            }
        }
        ZOBRIST_TURN = rng();
    }
} // end namespace Teammate