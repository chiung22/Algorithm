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

namespace Chiung {

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
    struct TTEntry { uint64_t hash = 0; int depth = -1; int value = 0; TTFlag flag = EXACT; };

    inline static vector<TTEntry> TT_Cache(TT_SIZE);
    inline static mutex ttMutex;
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
            return empty == 0 || p1 == 0 || p2 == 0;
        }
        int getWinner() const {
            int c1 = popcount64(p1); int c2 = popcount64(p2);
            return (c1 > c2) ? PLAYER1 : (c2 > c1 ? PLAYER2 : 0);
        }
    };

    class AtaxxEngine {
    public:
        inline static atomic<long long> totalGameNodes{ 0 };
        inline static atomic<long long> ttHits{ 0 };
        inline static atomic<long long> ttLookups{ 0 };

        inline static Move bestMoveOverall;
        inline static int highestReachedDepth = 0;
        inline static mutex bestMoveMutex;

        // =====================================================================
        // 🔥 V10 진화형 평가 함수 (킬각 + 진로 방해 + 코너 장악 + 중앙 교착 타파)
        // =====================================================================
        static int evaluate(const Bitboard& b, int myColor) {
            uint64_t my = (myColor == PLAYER1) ? b.p1 : b.p2;
            uint64_t opp = (myColor == PLAYER1) ? b.p2 : b.p1;
            uint64_t empty = ~(b.p1 | b.p2) & FULL_BOARD_MASK;

            int myCount = popcount64(my);
            int oppCount = popcount64(opp);
            int total = myCount + oppCount;

            // 1. [약점 극복] 킬각 강제 인식 (스탯 농사 및 능욕 방지)
            if (oppCount == 0) return INF;   // 적 전멸 시 무조건 100만점 (즉시 승리)
            if (myCount == 0) return -INF;   // 내 전멸 시 -100만점 (절대 회피)
            if (total == 0) return 0;

            double myPercent = ((double)myCount / total) * 100.0;
            int pScore = myCount - oppCount;

            // 2. [약점 극복] 나와 상대의 '기동성/밀집도 차이(Diff)' 계산 (섀도복싱 방지 및 진로 차단)
            int myMobility = 0, myDensity = 0;
            uint64_t temp = my;
            while (temp) {
                int idx = ctz64(temp);
                myDensity += popcount64(ADJ_MASK[idx] & my);
                myMobility += popcount64((ADJ_MASK[idx] | JUMP_MASK[idx]) & empty);
                temp &= temp - 1;
            }

            int oppMobility = 0, oppDensity = 0;
            temp = opp;
            while (temp) {
                int idx = ctz64(temp);
                oppDensity += popcount64(ADJ_MASK[idx] & opp);
                oppMobility += popcount64((ADJ_MASK[idx] | JUMP_MASK[idx]) & empty);
                temp &= temp - 1;
            }

            // 내가 갈 수 있는 곳 - 적이 갈 수 있는 곳 = 상대방 억제력
            int mDiff = myMobility - oppMobility;
            int dDiff = myDensity - oppDensity;

            // 3. [약점 극복] 절대 방어 구역(코너) 장악 가중치 추가
            // 인덱스 0(좌상단), 6(우상단), 42(좌하단), 48(우하단)
            uint64_t corners = (1ULL << 0) | (1ULL << 6) | (1ULL << 42) | (1ULL << 48);
            int cDiff = popcount64(my & corners) - popcount64(opp & corners);

            // 4. [약점 극복] 무승부 방지용 교착 상태 타파 (중앙 장악력 미세 보너스)
            // 인덱스 16, 17, 18, 23, 24, 25, 30, 31, 32 (중앙 3x3 구역)
            uint64_t center = 0x10E070000ULL;
            int centerDiff = popcount64(my & center) - popcount64(opp & center);

            // 5. 업그레이드된 유불리 동적 휴리스틱 (페이즈 분리 적용)
            if (myPercent < 45.0) {
                // 불리할 때: 돌 차이(10) 무시, 무조건 적 길막(mDiff 70) 및 뭉치기(dDiff 50), 코너 확보(200)에 올인
                return pScore * 10 + mDiff * 70 + dDiff * 50 + cDiff * 200 + centerDiff * 5;
            }
            else if (myPercent <= 55.0) {
                // 팽팽할 때: 돌 차이(20)보다 상대 억제(mDiff 90)에 치중하며 난전 유도
                return pScore * 20 + mDiff * 90 + dDiff * 10 + cDiff * 200 + centerDiff * 10;
            }
            else {
                // 유리할 때: 기교 부리지 말고 확실하게 돌 개수 격차(120)를 벌려 압살
                return pScore * 120 + mDiff * 20 + cDiff * 100 + centerDiff * 2;
            }
        }
        // =====================================================================

        static int minimax(const Bitboard& b, int depth, int alpha, int beta, bool isMax, int myColor) {
            if (searchCancelled) return 0;

            totalGameNodes.fetch_add(1, memory_order_relaxed);

            int alphaOrig = alpha;
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

            if (depth == 0 || b.isGameOver()) return evaluate(b, myColor);

            int currColor = isMax ? myColor : (myColor == PLAYER1 ? PLAYER2 : PLAYER1);
            vector<Move> moves = getValidMoves(b, currColor);
            if (moves.empty()) return evaluate(b, myColor);

            int best = isMax ? -INF : INF;
            for (auto& m : moves) {
                Bitboard nextB = applyMove(b, m, currColor);
                int val = minimax(nextB, depth - 1, alpha, beta, !isMax, myColor);

                if (searchCancelled) return 0;

                if (isMax) { best = max(best, val); alpha = max(alpha, best); }
                else { best = min(best, val); beta = min(beta, best); }
                if (beta <= alpha) break;
            }

            {
                lock_guard<mutex> lock(ttMutex);
                TTEntry& tte = TT_Cache[ttIndex];
                tte.hash = b.hashKey; tte.depth = depth; tte.value = best;
                if (best <= alphaOrig) tte.flag = UPPERBOUND;
                else if (best >= beta) tte.flag = LOWERBOUND;
                else tte.flag = EXACT;
            }
            return best;
        }

        static vector<Move> getValidMoves(const Bitboard& b, int color) {
            vector<Move> moves;
            uint64_t my = (color == PLAYER1) ? b.p1 : b.p2;
            uint64_t opp = (color == PLAYER1) ? b.p2 : b.p1;
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
            uint64_t* my = (color == PLAYER1) ? &b.p1 : &b.p2;
            uint64_t* opp = (color == PLAYER1) ? &b.p2 : &b.p1;
            int myIdx = (color == PLAYER1) ? 0 : 1;
            int oppIdx = (color == PLAYER1) ? 1 : 0;

            if (!m.isClone) { *my &= ~(1ULL << m.from); b.hashKey ^= ZOBRIST_TABLE[m.from][myIdx]; }
            *my |= (1ULL << m.to); b.hashKey ^= ZOBRIST_TABLE[m.to][myIdx];

            uint64_t caps = ADJ_MASK[m.to] & *opp;
            *my |= caps; *opp &= ~caps;

            uint64_t capTemp = caps;
            while (capTemp) {
                int idx = ctz64(capTemp);
                b.hashKey ^= ZOBRIST_TABLE[idx][oppIdx]; b.hashKey ^= ZOBRIST_TABLE[idx][myIdx];
                capTemp &= capTemp - 1;
            }
            b.hashKey ^= ZOBRIST_TURN;
            return b;
        }

        static Move getBestMoveFixedDepth(const Bitboard& board, int myColor, int targetDepth) {
            searchCancelled = false;
            totalGameNodes = 0; ttHits = 0; ttLookups = 0;

            vector<Move> validMoves = getValidMoves(board, myColor);
            if (validMoves.empty()) return { -1, -1, false, 0, 0 };

            Move globalBestMove = validMoves[0];

            for (int currentDepth = 1; currentDepth <= targetDepth; currentDepth++) {
                Move depthBestMove = globalBestMove;
                int bestScore = -INF;

                auto it = find_if(validMoves.begin(), validMoves.end(), [&](const Move& m) {
                    return m.from == globalBestMove.from && m.to == globalBestMove.to && m.isClone == globalBestMove.isClone;
                    });
                if (it != validMoves.end() && it != validMoves.begin()) iter_swap(validMoves.begin(), it);

                for (const Move& m : validMoves) {
                    Bitboard nextB = applyMove(board, m, myColor);
                    int score = minimax(nextB, currentDepth - 1, -INF, INF, false, myColor);
                    if (score > bestScore) { bestScore = score; depthBestMove = m; }
                }
                globalBestMove = depthBestMove;
            }
            return globalBestMove;
        }

        static Move getBestMoveTimeLimited(const Bitboard& b, int myColor, double timeLimitSeconds) {
            searchCancelled = false;
            totalGameNodes = 0; ttHits = 0; ttLookups = 0; highestReachedDepth = 0;

            vector<Move> validMoves = getValidMoves(b, myColor);
            if (validMoves.empty()) return { -1, -1, false, 0, 0 };
            if (validMoves.size() == 1) return validMoves[0];

            bestMoveOverall = validMoves[0];

            int numThreads = (int)std::thread::hardware_concurrency();
            if (numThreads == 0) numThreads = 4;

            vector<thread> workers;

            for (int t = 0; t < numThreads; t++) {
                workers.emplace_back([&, t, validMoves, myColor, b]() {
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
                            int eval = minimax(nextB, depth - 1, alpha, beta, false, myColor);

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

        static void initMasks() {
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
    };
} // end namespace Chiung