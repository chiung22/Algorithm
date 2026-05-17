#pragma once
#include <iostream>
#include <vector>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <thread>
#include <atomic>
#include <cmath>
#include <iomanip>

// [호환성 패치] 윈도우(MSVC) 환경에서도 에러 없이 구동
#ifdef _MSC_VER
#include <intrin.h>
#define __builtin_popcountll __popcnt64
inline int __builtin_ctzll(uint64_t mask) { unsigned long index; _BitScanForward64(&index, mask); return (int)index; }
#endif

using namespace std;
using namespace std::chrono;

// =====================================================================
// [상수 및 마스크 정의]
// =====================================================================
const uint64_t FULL_BOARD_MASK = 0x1FFFFFFFFFFFFULL;
const int PLAYER1 = 1; // 치웅 AI (흑돌)
const int PLAYER2 = 2; // 팀원 AI (백돌)

static uint64_t ADJACENT_MASK[49];
static uint64_t JUMP_MASK[49];
static uint64_t ZOBRIST_TABLE[49][2];
static uint64_t ZOBRIST_TURN;

enum TTFlag { EXACT, LOWERBOUND, UPPERBOUND };
struct TTEntry { uint64_t hash; int depth; int flag; int value; };
const int TT_SIZE = 1048576;
static vector<TTEntry> TT_Cache(TT_SIZE);

static atomic<bool> searchCancelled(false);
static atomic<bool> ponderCancelled(false);

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

// =====================================================================
// [Chiung V9 AI 코어 엔진]
// =====================================================================
class ChiungEngine {
public:
    static long long ttHits;
    static long long ttLookups;
    static long long totalGameNodes;

    // 유불리 기반 동적 휴리스틱 (방어=1, 기동=2, 물량=3 최적 조합 하드코딩)
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
        int mScore = 0, dScore = 0;

        uint64_t temp = my;
        while (temp) {
            int idx = __builtin_ctzll(temp);
            dScore += __builtin_popcountll(ADJACENT_MASK[idx] & my);
            mScore += __builtin_popcountll((ADJACENT_MASK[idx] | JUMP_MASK[idx]) & empty);
            temp &= temp - 1;
        }

        if (myPercent < 45.0) return pScore * 10 + dScore * 60;       // 불리: 방어
        else if (myPercent <= 55.0) return pScore * 20 + mScore * 80; // 비등: 기동
        else return pScore * 100;                                     // 유리: 물량
    }

    static int minimax(const Bitboard& b, int depth, int alpha, int beta, bool isMax, int myColor) {
        totalGameNodes++;

        int alphaOrig = alpha;
        int ttIndex = b.hashKey & (TT_SIZE - 1);
        TTEntry& tte = TT_Cache[ttIndex];

        ttLookups++;
        if (tte.hash == b.hashKey && tte.depth >= depth) {
            ttHits++;
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

            if (isMax) { best = max(best, val); alpha = max(alpha, best); }
            else { best = min(best, val); beta = min(beta, best); }
            if (beta <= alpha) break;
        }

        tte.hash = b.hashKey; tte.depth = depth; tte.value = best;
        if (best <= alphaOrig) tte.flag = UPPERBOUND;
        else if (best >= beta) tte.flag = LOWERBOUND;
        else tte.flag = EXACT;

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
};

// static 변수 초기화 (헤더 온리 구성을 위해 하단에 배치)
long long ChiungEngine::ttHits = 0;
long long ChiungEngine::ttLookups = 0;
long long ChiungEngine::totalGameNodes = 0;