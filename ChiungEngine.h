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
#include <cstring>

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
    const int TT_LOCKS = 4096;

    inline static uint64_t ADJ_MASK[49];
    inline static uint64_t JUMP_MASK[49];
    inline static uint64_t ZOBRIST_TABLE[49][2];
    inline static uint64_t ZOBRIST_TURN;

    enum TTFlag { EXACT, LOWERBOUND, UPPERBOUND };

    struct Move {
        int from = -1, to = -1; bool isClone = false; int infectCount = 0; int orderScore = 0;
        bool operator<(const Move& other) const { return orderScore > other.orderScore; }
        bool operator==(const Move& other) const { return from == other.from && to == other.to && isClone == other.isClone; }
    };

    struct TTEntry {
        uint64_t hash; int depth; int value; TTFlag flag; Move bestMove;
        TTEntry(uint64_t h = 0, int d = -1, int v = 0, TTFlag f = EXACT, Move m = Move())
            : hash(h), depth(d), value(v), flag(f), bestMove(m) {}
    };

    inline static vector<TTEntry> TT_Cache(TT_SIZE);
    struct MutexWrapper { std::mutex m; };
    inline static MutexWrapper ttMutexes[TT_LOCKS];

    inline static atomic<bool> searchCancelled{ false };
    inline static int historyTable[3][49][49];
    inline static Move killerMoves[128][2];

    inline static atomic<bool> stopPonder{ false };
    inline static vector<thread> ponderWorkers;

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
        inline static Move bestMoveOverall;
        inline static int highestReachedDepth = 0;
        inline static mutex bestMoveMutex;

        inline static const int W_ORDER_BONUS = 300;

        static bool hasValidMoves(const Bitboard& b, int color) {
            uint64_t my = (color == PLAYER1) ? b.p1 : b.p2;
            uint64_t empty = ~(b.p1 | b.p2) & FULL_BOARD_MASK;
            uint64_t temp = my;
            while (temp) {
                int idx = ctz64(temp);
                if ((ADJ_MASK[idx] | JUMP_MASK[idx]) & empty) return true;
                temp &= temp - 1;
            }
            return false;
        }

        static int evaluate(const Bitboard& b, int myColor) {
            uint64_t my = (myColor == PLAYER1) ? b.p1 : b.p2;
            uint64_t opp = (myColor == PLAYER1) ? b.p2 : b.p1;
            uint64_t empty = ~(b.p1 | b.p2) & FULL_BOARD_MASK;

            int myCount = popcount64(my);
            int oppCount = popcount64(opp);
            int emptyCount = popcount64(empty);

            if (oppCount == 0) return INF;
            if (myCount == 0) return -INF;
            if (!hasValidMoves(b, (myColor == PLAYER1 ? PLAYER2 : PLAYER1))) return INF;
            if (!hasValidMoves(b, myColor)) return -INF;

            int pScore = myCount - oppCount;
            // 극후반부에는 돌의 개수가 전부이므로 점수 증폭
            if (emptyCount <= 14) return pScore * 100000;

            int myMobility = 0, oppMobility = 0;
            uint64_t temp = my;
            while (temp) {
                int idx = ctz64(temp);
                myMobility += popcount64((ADJ_MASK[idx] | JUMP_MASK[idx]) & empty);
                temp &= temp - 1;
            }
            temp = opp;
            while (temp) {
                int idx = ctz64(temp);
                oppMobility += popcount64((ADJ_MASK[idx] | JUMP_MASK[idx]) & empty);
                temp &= temp - 1;
            }

            int mDiff = myMobility - oppMobility;
            
            // 코너 마스크 (가장 중요한 절대 방어 구역)
            uint64_t corners = (1ULL << 0) | (1ULL << 6) | (1ULL << 42) | (1ULL << 48);
            int cDiff = popcount64(my & corners) - popcount64(opp & corners);

            // 위험 구역 마스크 (코너와 인접하여 상대에게 코너를 내어줄 수 있는 X-C 구역)
            uint64_t danger = (1ULL << 1) | (1ULL << 5) | (1ULL << 7) | (1ULL << 8) | (1ULL << 12) | (1ULL << 13) |
                (1ULL << 35) | (1ULL << 36) | (1ULL << 40) | (1ULL << 41) | (1ULL << 43) | (1ULL << 47);
            // 내가 위험구역을 덜 차지할수록(+), 상대가 많이 차지할수록(+) 이득
            int dangerDiff = popcount64(opp & danger) - popcount64(my & danger);
            
            // 가장자리(Edge) 마스크 (코너 다음으로 안정적인 구역)
            uint64_t edges = 0x1FC0000000000ULL | 0x7F | 0x1020408102040ULL | 0x2040810204080ULL;
            edges &= ~(corners | danger); // 코너와 위험구역 제외
            int edgeDiff = popcount64(my & edges) - popcount64(opp & edges);

            // [핵심 변경] 태완 엔진(코너 가중치 430)을 압살하기 위한 극단적 코너 지배 가중치
            int cWeight = 3500 + (emptyCount * 20); // 코너 하나가 돌 30~50개 이상의 가치를 가짐
            int dangerWeight = 1200;                // 코너 주변에 두는 행위 자체를 극도로 혐오하게 만듦
            int edgeWeight = 200;                   // 외곽선 점유 보너스 추가
            int mWeight = (emptyCount * 3);         // 초반 기동력 중시
            int pWeight = 100 + (49 - emptyCount) * 6; // 중후반 돌 개수 가중치

            return pScore * pWeight + mDiff * mWeight + cDiff * cWeight + edgeDiff * edgeWeight + dangerDiff * dangerWeight;
        }

        static int pvs(const Bitboard& b, int depth, int alpha, int beta, bool isMax, int myColor, bool isNullMoveAllowed) {
            if (searchCancelled) return 0;
            totalGameNodes.fetch_add(1, memory_order_relaxed);

            int alphaOrig = alpha;
            int ttIndex = (int)(b.hashKey & (TT_SIZE - 1));
            int lockIndex = ttIndex & (TT_LOCKS - 1);
            Move ttBestMove;
            bool hasTtMove = false;

            {
                lock_guard<mutex> lock(ttMutexes[lockIndex].m);
                TTEntry& tte = TT_Cache[ttIndex];
                if (tte.hash == b.hashKey) {
                    if (tte.bestMove.to != -1) { ttBestMove = tte.bestMove; hasTtMove = true; }
                    if (tte.depth >= depth) {
                        if (tte.flag == EXACT) return tte.value;
                        if (tte.flag == LOWERBOUND) alpha = max(alpha, tte.value);
                        if (tte.flag == UPPERBOUND) beta = min(beta, tte.value);
                        if (alpha >= beta) return tte.value;
                    }
                }
            }

            if (depth <= 0 || b.isGameOver()) return evaluate(b, myColor);
            int currColor = isMax ? myColor : (myColor == PLAYER1 ? PLAYER2 : PLAYER1);

            if (!isMax && depth <= 3 && !hasTtMove) {
                int staticEval = evaluate(b, myColor);
                int margin = 150 * depth;
                if (staticEval - margin >= beta) return staticEval - margin;
            }

            if (isNullMoveAllowed && depth >= 3 && hasValidMoves(b, currColor)) {
                Bitboard nullBoard = b; nullBoard.hashKey ^= ZOBRIST_TURN;
                int nullVal = pvs(nullBoard, depth - 1 - 2, alpha, beta, !isMax, myColor, false);
                if (nullVal >= beta) return beta;
            }

            vector<Move> moves = getValidMoves(b, currColor, depth);
            if (moves.empty()) return evaluate(b, myColor);

            if (hasTtMove) {
                auto it = find(moves.begin(), moves.end(), ttBestMove);
                if (it != moves.end() && it != moves.begin()) iter_swap(moves.begin(), it);
            }

            int best = isMax ? -INF : INF;
            Move currentBestMove = moves[0];
            bool firstMove = true;
            int movesSearched = 0;

            for (auto& m : moves) {
                Bitboard nextB = applyMove(b, m, currColor);
                int val = 0;
                bool isCapture = m.infectCount > 0;
                bool isKiller = (m == killerMoves[depth][0] || m == killerMoves[depth][1]);
                
                int reduction = 0;
                if (depth >= 3 && movesSearched >= 3 && !isCapture && !isKiller && !m.isClone) {
                    reduction = 1 + (movesSearched / 6) + (depth / 6);
                    if (reduction >= depth) reduction = depth - 1;
                }

                if (firstMove) {
                    val = pvs(nextB, depth - 1, alpha, beta, !isMax, myColor, true);
                    firstMove = false;
                }
                else {
                    val = pvs(nextB, depth - 1 - reduction, alpha, alpha + 1, !isMax, myColor, true);
                    if (val > alpha && reduction > 0) val = pvs(nextB, depth - 1, alpha, alpha + 1, !isMax, myColor, true);
                    if (val > alpha && val < beta) val = pvs(nextB, depth - 1, alpha, beta, !isMax, myColor, true);
                }

                if (searchCancelled) return 0;

                if (isMax) { if (val > best) { best = val; currentBestMove = m; } alpha = max(alpha, best); }
                else { if (val < best) { best = val; currentBestMove = m; } beta = min(beta, best); }

                movesSearched++;
                if (beta <= alpha) {
                    if (m.infectCount == 0) {
                        historyTable[currColor][m.from][m.to] += depth * depth;
                        if (!(m == killerMoves[depth][0])) { killerMoves[depth][1] = killerMoves[depth][0]; killerMoves[depth][0] = m; }
                    }
                    break;
                }
            }

            {
                lock_guard<mutex> lock(ttMutexes[lockIndex].m);
                TTEntry& tte = TT_Cache[ttIndex];
                tte.hash = b.hashKey; tte.depth = depth; tte.value = best; tte.bestMove = currentBestMove;
                if (best <= alphaOrig) tte.flag = UPPERBOUND; else if (best >= beta) tte.flag = LOWERBOUND; else tte.flag = EXACT;
            }
            return best;
        }

        static int pvsPonder(const Bitboard& b, int depth, int alpha, int beta, bool isMax, int myColor, bool isNullMoveAllowed) {
            if (stopPonder) return 0;

            int alphaOrig = alpha;
            int ttIndex = (int)(b.hashKey & (TT_SIZE - 1));
            int lockIndex = ttIndex & (TT_LOCKS - 1);
            Move ttBestMove;
            bool hasTtMove = false;

            {
                lock_guard<mutex> lock(ttMutexes[lockIndex].m);
                TTEntry& tte = TT_Cache[ttIndex];
                if (tte.hash == b.hashKey) {
                    if (tte.bestMove.to != -1) { ttBestMove = tte.bestMove; hasTtMove = true; }
                    if (tte.depth >= depth) {
                        if (tte.flag == EXACT) return tte.value;
                        if (tte.flag == LOWERBOUND) alpha = max(alpha, tte.value);
                        if (tte.flag == UPPERBOUND) beta = min(beta, tte.value);
                        if (alpha >= beta) return tte.value;
                    }
                }
            }

            if (depth <= 0 || b.isGameOver()) return evaluate(b, myColor);
            int currColor = isMax ? myColor : (myColor == PLAYER1 ? PLAYER2 : PLAYER1);

            vector<Move> moves = getValidMoves(b, currColor, depth);
            if (moves.empty()) return evaluate(b, myColor);

            if (hasTtMove) {
                auto it = find(moves.begin(), moves.end(), ttBestMove);
                if (it != moves.end() && it != moves.begin()) iter_swap(moves.begin(), it);
            }

            int best = isMax ? -INF : INF;
            Move currentBestMove = moves[0];
            bool firstMove = true;

            for (auto& m : moves) {
                Bitboard nextB = applyMove(b, m, currColor);
                int val = 0;

                if (firstMove) {
                    val = pvsPonder(nextB, depth - 1, alpha, beta, !isMax, myColor, true);
                    firstMove = false;
                }
                else {
                    val = pvsPonder(nextB, depth - 1, alpha, alpha + 1, !isMax, myColor, true);
                    if (val > alpha && val < beta) val = pvsPonder(nextB, depth - 1, alpha, beta, !isMax, myColor, true);
                }

                if (stopPonder) return 0;

                if (isMax) { if (val > best) { best = val; currentBestMove = m; } alpha = max(alpha, best); }
                else { if (val < best) { best = val; currentBestMove = m; } beta = min(beta, best); }

                if (beta <= alpha) break;
            }

            {
                lock_guard<mutex> lock(ttMutexes[lockIndex].m);
                TTEntry& tte = TT_Cache[ttIndex];
                if (depth > tte.depth) {
                    tte.hash = b.hashKey; tte.depth = depth; tte.value = best; tte.bestMove = currentBestMove;
                    if (best <= alphaOrig) tte.flag = UPPERBOUND; else if (best >= beta) tte.flag = LOWERBOUND; else tte.flag = EXACT;
                }
            }
            return best;
        }

        static vector<Move> getValidMoves(const Bitboard& b, int color, int currentDepth) {
            vector<Move> moves;
            uint64_t my = (color == PLAYER1) ? b.p1 : b.p2;
            uint64_t opp = (color == PLAYER1) ? b.p2 : b.p1;
            uint64_t empty = ~(b.p1 | b.p2) & FULL_BOARD_MASK;

            uint64_t corners = (1ULL << 0) | (1ULL << 6) | (1ULL << 42) | (1ULL << 48);
            uint64_t danger = (1ULL << 1) | (1ULL << 5) | (1ULL << 7) | (1ULL << 8) | (1ULL << 12) | (1ULL << 13) |
                (1ULL << 35) | (1ULL << 36) | (1ULL << 40) | (1ULL << 41) | (1ULL << 43) | (1ULL << 47);

            uint64_t temp = my;
            while (temp) {
                int from = ctz64(temp);
                uint64_t clones = ADJ_MASK[from] & empty;
                while (clones) {
                    int to = ctz64(clones);
                    int caps = popcount64(ADJ_MASK[to] & opp);

                    int orderScore = 0;
                    if (caps > 0) orderScore = 1000000 + caps * 10000;
                    else {
                        Move tempMove = { from, to, true, 0, 0 };
                        if (currentDepth < 128) {
                            if (tempMove == killerMoves[currentDepth][0]) orderScore += 90000;
                            else if (tempMove == killerMoves[currentDepth][1]) orderScore += 80000;
                        }
                        orderScore += historyTable[color][from][to];
                    }

                    // [핵심 변경] Move Ordering 최우선 탐색으로 코너 선점 절대화
                    if ((1ULL << to) & corners) orderScore += 500000;
                    if ((1ULL << to) & danger) orderScore -= 100000;
                    orderScore += W_ORDER_BONUS;

                    moves.push_back({ from, to, true, caps, orderScore });
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

                    int orderScore = 0;
                    if (caps > 0) orderScore = 1000000 + caps * 10000;
                    else {
                        Move tempMove = { from, to, false, 0, 0 };
                        if (currentDepth < 128) {
                            if (tempMove == killerMoves[currentDepth][0]) orderScore += 90000;
                            else if (tempMove == killerMoves[currentDepth][1]) orderScore += 80000;
                        }
                        orderScore += historyTable[color][from][to];
                    }

                    if ((1ULL << to) & corners) orderScore += 500000;
                    if ((1ULL << to) & danger) orderScore -= 100000;

                    moves.push_back({ from, to, false, caps, orderScore });
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

        static Move getBestMoveTimeLimited(const Bitboard& b, int myColor, double timeLimitSeconds) {

            stopPonder = true;
            for (auto& w : ponderWorkers) { if (w.joinable()) w.join(); }
            ponderWorkers.clear();

            searchCancelled = false;
            totalGameNodes = 0; highestReachedDepth = 0;

            memset(historyTable, 0, sizeof(historyTable));
            memset(killerMoves, 0, sizeof(killerMoves));

            vector<Move> validMoves = getValidMoves(b, myColor, 0);
            if (validMoves.empty()) return { -1, -1, false, 0, 0 };
            if (validMoves.size() == 1) return validMoves[0];

            bestMoveOverall = validMoves[0];

            int numThreads = (int)std::thread::hardware_concurrency();
            if (numThreads == 0) numThreads = 4;

            vector<thread> workers;

            for (int t = 0; t < numThreads; t++) {
                workers.emplace_back([&, t, validMoves, myColor, b]() {
                    vector<Move> threadMoves = validMoves;
                    if (t > 0) { mt19937 rng(12345 + t); shuffle(threadMoves.begin() + 1, threadMoves.end(), rng); }

                    Move localBestMove = threadMoves[0];
                    int prevScore = 0;

                    for (int depth = 1; depth <= 100; depth++) {

                        int delta = 50; 
                        int alphaOrig = -INF;
                        int betaOrig = INF;
                        if (depth > 2) {
                            alphaOrig = prevScore - delta;
                            betaOrig = prevScore + delta;
                        }
                        int alpha = alphaOrig;
                        int beta = betaOrig;

                        while (true) {
                            int maxEval = -INF; Move depthBestMove = threadMoves[0];
                            auto it = find(threadMoves.begin(), threadMoves.end(), localBestMove);
                            if (it != threadMoves.end() && it != threadMoves.begin()) iter_swap(threadMoves.begin(), it);

                            bool rootFirst = true;
                            for (const Move& m : threadMoves) {
                                Bitboard nextB = applyMove(b, m, myColor);
                                int eval = 0;

                                if (rootFirst) { eval = pvs(nextB, depth - 1, alpha, beta, false, myColor, true); rootFirst = false; }
                                else {
                                    eval = pvs(nextB, depth - 1, alpha, alpha + 1, false, myColor, true);
                                    if (eval > alpha && eval < beta) eval = pvs(nextB, depth - 1, alpha, beta, false, myColor, true);
                                }

                                if (searchCancelled) break;
                                if (eval > maxEval) { maxEval = eval; depthBestMove = m; }
                                alpha = max(alpha, eval);
                            }

                            if (searchCancelled) break;

                            if (maxEval <= alphaOrig) {
                                delta *= 3;
                                alphaOrig = max(-INF, prevScore - delta);
                                alpha = alphaOrig;
                                continue;
                            }
                            else if (maxEval >= betaOrig) {
                                delta *= 3;
                                betaOrig = min(INF, prevScore + delta);
                                beta = betaOrig;
                                continue;
                            }

                            localBestMove = depthBestMove; prevScore = maxEval;
                            break;
                        }

                        if (searchCancelled) break;

                        lock_guard<mutex> lock(bestMoveMutex);
                        if (depth > highestReachedDepth || (t == 0 && depth == highestReachedDepth)) {
                            highestReachedDepth = depth; bestMoveOverall = localBestMove;
                        }
                    }
                    });
            }

            auto start = high_resolution_clock::now();
            while (duration<double>(high_resolution_clock::now() - start).count() < timeLimitSeconds) {
                this_thread::sleep_for(milliseconds(5));
            }
            searchCancelled = true;
            for (auto& w : workers) { if (w.joinable()) w.join(); }

            Bitboard nextB = applyMove(b, bestMoveOverall, myColor);
            int oppColor = (myColor == PLAYER1) ? PLAYER2 : PLAYER1;

            int ttIndex = (int)(nextB.hashKey & (TT_SIZE - 1));
            Move predictedOppMove = TT_Cache[ttIndex].bestMove;

            if (predictedOppMove.to != -1 && !nextB.isGameOver()) {
                Bitboard ponderBoard = applyMove(nextB, predictedOppMove, oppColor);
                stopPonder = false;

                for (int t = 0; t < numThreads; t++) {
                    ponderWorkers.emplace_back([ponderBoard, myColor, t]() {
                        for (int d = 1; d <= 25; d++) {
                            if (stopPonder) break;
                            pvsPonder(ponderBoard, d, -INF, INF, true, myColor, true);
                        }
                        });
                }
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
}
