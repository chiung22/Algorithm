#pragma once
#include <iostream>
#include <vector>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <random>
#include <thread>
#include <atomic>
#include <mutex>
#include <cmath>
#include <iomanip>

// [에러 수정] 윈도우 환경에서 64비트 비트 카운팅 함수 호환성 및 int 캐스팅 형변환 완벽 처리
#ifdef _MSC_VER
#include <intrin.h>
#define __builtin_popcountll(x) (int)__popcnt64(x)
inline int __builtin_ctzll(uint64_t mask) { unsigned long index; _BitScanForward64(&index, mask); return (int)index; }
#endif

using namespace std;
using namespace std::chrono;

namespace Chiung {

    const uint64_t FULL_BOARD_MASK = 0x1FFFFFFFFFFFFULL;
    const int PLAYER1 = 1;
    const int PLAYER2 = 2;

    inline static uint64_t ADJACENT_MASK[49];
    inline static uint64_t JUMP_MASK[49];
    inline static uint64_t ZOBRIST_TABLE[49][2];
    inline static uint64_t ZOBRIST_TURN;

    enum TTFlag { EXACT, LOWERBOUND, UPPERBOUND };

    struct TTEntry { uint64_t hash; int depth; int flag; int value; };
    const int TT_SIZE = 1048576;
    inline static vector<TTEntry> TT_Cache(TT_SIZE);

    inline static atomic<bool> searchCancelled{ false };
    inline static atomic<bool> ponderCancelled{ false };

    struct Move {
        int from, to; bool isClone; int infectCount; int orderScore;
        bool operator<(const Move& other) const { return orderScore > other.orderScore; }
        bool operator==(const Move& other) const { return from == other.from && to == other.to && isClone == other.isClone; }
    };

    struct Bitboard {
        uint64_t p1, p2, hashKey;
        void init() {
            p1 = 0; p2 = 0;
            p1 |= (1ULL << 0); p1 |= (1ULL << 48);
            p2 |= (1ULL << 6); p2 |= (1ULL << 42);
            hashKey = 0;
            hashKey ^= ZOBRIST_TABLE[0][0]; hashKey ^= ZOBRIST_TABLE[48][0];
            hashKey ^= ZOBRIST_TABLE[6][1]; hashKey ^= ZOBRIST_TABLE[42][1];
            hashKey ^= ZOBRIST_TURN;
        }
        bool isGameOver() const {
            uint64_t empty = ~(p1 | p2) & FULL_BOARD_MASK;
            return (empty == 0 || p1 == 0 || p2 == 0);
        }
        int getWinner() const {
            int c1 = __builtin_popcountll(p1); int c2 = __builtin_popcountll(p2);
            return (c1 > c2) ? PLAYER1 : (c2 > c1 ? PLAYER2 : 0);
        }
    };

    class AtaxxEngine {
    public:
        // [구문 오류 수정] 소괄호 초기화 문법을 중괄호 초기화 형태로 변경하여 함수 선언 오인(Most Vexing Parse) 차단
        inline static atomic<long long> totalGameNodes{ 0 };
        inline static atomic<long long> ttHits{ 0 };
        inline static atomic<long long> ttLookups{ 0 };

        inline static Move bestMoveOverall;
        inline static int highestReachedDepth = 0;
        inline static mutex bestMoveMutex;

        static int evaluate(const Bitboard& b, int myColor) {
            uint64_t my = (myColor == PLAYER1) ? b.p1 : b.p2;
            uint64_t opp = (myColor == PLAYER1) ? b.p2 : b.p1;
            uint64_t empty = ~(b.p1 | b.p2) & FULL_BOARD_MASK;

            int myCount = __builtin_popcountll(my);
            int oppCount = __builtin_popcountll(opp);
            int total = myCount + oppCount;
            if (total == 0) return 0;

            double myPercent = ((double)myCount / total) * 100.0;
            int pScore = myCount - oppCount;
            int mobilityScore = 0, densityScore = 0;

            uint64_t temp = my;
            while (temp) {
                int idx = __builtin_ctzll(temp);
                densityScore += __builtin_popcountll(ADJACENT_MASK[idx] & my);
                mobilityScore += __builtin_popcountll((ADJACENT_MASK[idx] | JUMP_MASK[idx]) & empty);
                temp &= temp - 1;
            }

            if (myPercent < 45.0) return pScore * 10 + densityScore * 60;
            else if (myPercent <= 55.0) return pScore * 20 + mobilityScore * 80;
            else return pScore * 100;
        }

        static int minimax(const Bitboard& b, int depth, int alpha, int beta, bool isMax, int myColor) {
            if (searchCancelled) return 0;

            totalGameNodes.fetch_add(1, memory_order_relaxed);

            int alphaOrig = alpha;
            int ttIndex = (int)(b.hashKey & (TT_SIZE - 1));
            TTEntry& tte = TT_Cache[ttIndex];

            ttLookups.fetch_add(1, memory_order_relaxed);
            if (tte.hash == b.hashKey && tte.depth >= depth) {
                ttHits.fetch_add(1, memory_order_relaxed);
                if (tte.flag == EXACT) return tte.value;
                if (tte.flag == LOWERBOUND && tte.value > alpha) alpha = tte.value;
                if (tte.flag == UPPERBOUND && tte.value < beta) beta = tte.value;
                if (alpha >= beta) return tte.value;
            }

            if (depth == 0 || b.isGameOver()) return evaluate(b, myColor);

            int currColor = isMax ? myColor : (myColor == PLAYER1 ? PLAYER2 : PLAYER1);
            vector<Move> moves = getValidMoves(b, currColor);
            if (moves.empty()) return evaluate(b, myColor);

            int best = isMax ? -1000000 : 1000000;

            for (auto& m : moves) {
                Bitboard nextB = applyMove(b, m, currColor);
                int val = minimax(nextB, depth - 1, alpha, beta, !isMax, myColor);

                if (searchCancelled) return 0;

                if (isMax) { best = max(best, val); alpha = max(alpha, best); }
                else { best = min(best, val); beta = min(beta, best); }
                if (beta <= alpha) break;
            }

            if (!searchCancelled) {
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

            uint64_t clones = 0, temp = my;
            while (temp) { clones |= ADJACENT_MASK[__builtin_ctzll(temp)]; temp &= temp - 1; }
            clones &= empty;

            while (clones) {
                int to = __builtin_ctzll(clones);
                int caps = __builtin_popcountll(ADJACENT_MASK[to] & opp);
                moves.push_back({ -1, to, true, caps, caps * 100 + 10 });
                clones &= clones - 1;
            }

            temp = my;
            while (temp) {
                int from = __builtin_ctzll(temp);
                uint64_t jumps = JUMP_MASK[from] & empty;
                while (jumps) {
                    int to = __builtin_ctzll(jumps);
                    int caps = __builtin_popcountll(ADJACENT_MASK[to] & opp);
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

            uint64_t caps = ADJACENT_MASK[m.to] & *opp;
            *my |= caps; *opp &= ~caps;

            uint64_t cap_temp = caps;
            while (cap_temp) {
                int idx = __builtin_ctzll(cap_temp);
                b.hashKey ^= ZOBRIST_TABLE[idx][oppIdx]; b.hashKey ^= ZOBRIST_TABLE[idx][myIdx];
                cap_temp &= cap_temp - 1;
            }
            b.hashKey ^= ZOBRIST_TURN;
            return b;
        }

        // [식별자 오류 수정] main.cpp 교차 검증에 필수적인 고정 깊이 탐색(Fixed Depth) 함수 완전체 구현 복구
        static Move getBestMoveFixedDepth(const Bitboard& b, int myColor, int targetDepth) {
            searchCancelled = false;
            ponderCancelled = false;
            totalGameNodes = 0;
            ttHits = 0;
            ttLookups = 0;

            vector<Move> validMoves = getValidMoves(b, myColor);
            if (validMoves.empty()) return { -1, -1, false, 0, 0 };

            Move globalBestMove = validMoves[0];

            // 반복 심화 결합으로 초기 알파-베타 컷오프 효율성 최적화
            for (int currentDepth = 1; currentDepth <= targetDepth; currentDepth++) {
                Move depthBestMove = validMoves[0];
                int maxEval = -1000000, alpha = -1000000, beta = 1000000;

                auto it = find(validMoves.begin(), validMoves.end(), globalBestMove);
                if (it != validMoves.end() && it != validMoves.begin()) {
                    iter_swap(validMoves.begin(), it);
                }

                for (const Move& m : validMoves) {
                    Bitboard nextB = applyMove(b, m, myColor);
                    int eval = minimax(nextB, currentDepth - 1, alpha, beta, false, myColor);
                    if (eval > maxEval) { maxEval = eval; depthBestMove = m; }
                    alpha = max(alpha, eval);
                }
                globalBestMove = depthBestMove;
            }
            return globalBestMove;
        }

        static Move getBestMoveTimeLimited(const Bitboard& b, int myColor, double timeLimitSeconds) {
            searchCancelled = false;
            totalGameNodes = 0;
            ttHits = 0;
            ttLookups = 0;
            highestReachedDepth = 0;

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
                        int maxEval = -1000000, alpha = -1000000, beta = 1000000;
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
                ADJACENT_MASK[i] = 0; JUMP_MASK[i] = 0;
                ZOBRIST_TABLE[i][0] = rng(); ZOBRIST_TABLE[i][1] = rng();
                int r = i / 7; int c = i % 7;
                for (int dr = -2; dr <= 2; dr++) {
                    for (int dc = -2; dc <= 2; dc++) {
                        if (dr == 0 && dc == 0) continue;
                        int nr = r + dr; int nc = c + dc;
                        if (nr >= 0 && nr < 7 && nc >= 0 && nc < 7) {
                            int nidx = nr * 7 + nc;
                            int dist = max(abs(dr), abs(dc));
                            if (dist == 1) ADJACENT_MASK[i] |= (1ULL << nidx);
                            else if (dist == 2) JUMP_MASK[i] |= (1ULL << nidx);
                        }
                    }
                }
            }
            ZOBRIST_TURN = rng();
        }
    };

} // end namespace Chiung