#pragma once
#include <iostream>
#include <vector>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <random>
#include <cmath>
#include <bitset>

#ifdef _MSC_VER
#include <intrin.h>
#endif

using namespace std;

namespace Teammate {

    const int SIZE = 7;
    const int CELL_COUNT = 49;
    const uint64_t FULL_BOARD_MASK = (1ULL << 49) - 1;

    const int PLAYER1 = 1;
    const int PLAYER2 = 2;

    const int INF = 1000000;
    const int TT_SIZE = 1048576;

    inline int popcount64(uint64_t x) { return (int)bitset<64>(x).count(); }
    inline int ctz64(uint64_t x) {
        int count = 0;
        while ((x & 1ULL) == 0) { x >>= 1; count++; }
        return count;
    }

    static uint64_t ADJACENT_MASK[49];
    static uint64_t JUMP_MASK[49];
    static uint64_t ZOBRIST_TABLE[49][2];
    static uint64_t ZOBRIST_TURN;

    enum TTFlag { EXACT, LOWERBOUND, UPPERBOUND };
    struct TTEntry { uint64_t hash = 0; int depth = -1; int flag = EXACT; int value = 0; };
    static vector<TTEntry> TT_Cache(TT_SIZE);

    struct Move {
        int from; int to; bool isClone; int infectCount; int orderScore;
        bool operator<(const Move& other) const { return orderScore > other.orderScore; }
    };

    struct Bitboard {
        uint64_t p1 = 0; uint64_t p2 = 0; uint64_t hashKey = 0;
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
            if (empty == 0 || p1 == 0 || p2 == 0) return true;
            return false;
        }
    };

    class AtaxxEngine {
    public:
        static long long ttHits;
        static long long ttLookups;
        static long long totalNodes;

        static int evaluate(const Bitboard& b, int myColor, int strat_dis, int strat_even, int strat_adv) {
            uint64_t my = (myColor == PLAYER1) ? b.p1 : b.p2;
            uint64_t opp = (myColor == PLAYER1) ? b.p2 : b.p1;
            uint64_t empty = ~(b.p1 | b.p2) & FULL_BOARD_MASK;

            int myCount = popcount64(my); int oppCount = popcount64(opp);
            int total = myCount + oppCount;

            if (oppCount == 0) return INF;
            if (myCount == 0) return -INF;
            if (total == 0) return 0;

            double myPercent = ((double)myCount / total) * 100.0;
            int pScore = myCount - oppCount;
            int mobilityScore = 0; int densityScore = 0;
            uint64_t temp = my;

            while (temp) {
                int idx = ctz64(temp);
                mobilityScore += popcount64((ADJACENT_MASK[idx] | JUMP_MASK[idx]) & empty);
                densityScore += popcount64(ADJACENT_MASK[idx] & my);
                temp &= temp - 1;
            }

            int currentStrategy = strat_even;
            if (myPercent < 45.0) currentStrategy = strat_dis;
            else if (myPercent > 55.0) currentStrategy = strat_adv;

            if (currentStrategy == 1) return pScore * 10 + densityScore * 60;
            else if (currentStrategy == 2) return pScore * 20 + mobilityScore * 80;
            else return pScore * 100;
        }

        static int minimax(const Bitboard& b, int depth, int alpha, int beta, bool isMax, int myColor, int strat_dis, int strat_even, int strat_adv) {
            totalNodes++;
            int alphaOrig = alpha; int betaOrig = beta;
            int ttIndex = b.hashKey & (TT_SIZE - 1);
            TTEntry& tte = TT_Cache[ttIndex];

            ttLookups++;
            if (tte.hash == b.hashKey && tte.depth >= depth) {
                ttHits++;
                if (tte.flag == EXACT) return tte.value;
                if (tte.flag == LOWERBOUND) alpha = max(alpha, tte.value);
                if (tte.flag == UPPERBOUND) beta = min(beta, tte.value);
                if (alpha >= beta) return tte.value;
            }

            if (depth == 0 || b.isGameOver()) return evaluate(b, myColor, strat_dis, strat_even, strat_adv);

            int currColor = isMax ? myColor : ((myColor == PLAYER1) ? PLAYER2 : PLAYER1);
            vector<Move> moves = getValidMoves(b, currColor);
            if (moves.empty()) return evaluate(b, myColor, strat_dis, strat_even, strat_adv);

            int best = isMax ? -INF : INF;
            for (auto& m : moves) {
                Bitboard nextB = applyMove(b, m, currColor);
                int val = minimax(nextB, depth - 1, alpha, beta, !isMax, myColor, strat_dis, strat_even, strat_adv);
                if (isMax) { best = max(best, val); alpha = max(alpha, best); }
                else { best = min(best, val); beta = min(beta, best); }
                if (beta <= alpha) break;
            }

            tte.hash = b.hashKey; tte.depth = depth; tte.value = best;
            if (best <= alphaOrig) tte.flag = UPPERBOUND;
            else if (best >= betaOrig) tte.flag = LOWERBOUND;
            else tte.flag = EXACT;

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
                uint64_t clones = ADJACENT_MASK[from] & empty;
                while (clones) {
                    int to = ctz64(clones);
                    int caps = popcount64(ADJACENT_MASK[to] & opp);
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
                    int caps = popcount64(ADJACENT_MASK[to] & opp);
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

            uint64_t captured = ADJACENT_MASK[m.to] & (*opp);
            *my |= captured; *opp &= ~captured;

            uint64_t capTemp = captured;
            while (capTemp) {
                int idx = ctz64(capTemp);
                b.hashKey ^= ZOBRIST_TABLE[idx][oppIdx]; b.hashKey ^= ZOBRIST_TABLE[idx][myIdx];
                capTemp &= capTemp - 1;
            }
            b.hashKey ^= ZOBRIST_TURN;
            return b;
        }
    };

    long long AtaxxEngine::ttHits = 0;
    long long AtaxxEngine::ttLookups = 0;
    long long AtaxxEngine::totalNodes = 0;

    class GPTAI {
    public:
        static Move getBestMove(const Bitboard& board, int color, int depth, int strat_dis, int strat_even, int strat_adv) {
            AtaxxEngine::totalNodes = 0;
            AtaxxEngine::ttHits = 0;
            AtaxxEngine::ttLookups = 0;

            vector<Move> moves = AtaxxEngine::getValidMoves(board, color);
            if (moves.empty()) return { -1, -1, false, 0, 0 };

            Move bestMove = moves[0];
            int bestScore = -INF;

            for (auto& m : moves) {
                Bitboard nextB = AtaxxEngine::applyMove(board, m, color);
                int score = AtaxxEngine::minimax(nextB, depth - 1, -INF, INF, false, color, strat_dis, strat_even, strat_adv);
                if (score > bestScore) { bestScore = score; bestMove = m; }
            }
            return bestMove;
        }
    };

    void initEngine() {
        mt19937_64 rng(12345);
        for (int i = 0; i < 49; i++) {
            ADJACENT_MASK[i] = 0; JUMP_MASK[i] = 0;
            ZOBRIST_TABLE[i][0] = rng(); ZOBRIST_TABLE[i][1] = rng();
            int r = i / 7; int c = i % 7;
            for (int dr = -2; dr <= 2; dr++) {
                for (int dc = -2; dc <= 2; dc++) {
                    if (dr == 0 && dc == 0) continue;
                    int nr = r + dr; int nc = c + dc;
                    if (nr < 0 || nr >= 7 || nc < 0 || nc >= 7) continue;
                    int nidx = nr * 7 + nc;
                    int dist = max(abs(dr), abs(dc));
                    if (dist == 1) ADJACENT_MASK[i] |= (1ULL << nidx);
                    else if (dist == 2) JUMP_MASK[i] |= (1ULL << nidx);
                }
            }
        }
        ZOBRIST_TURN = rng();
    }
} // end namespace Teammate