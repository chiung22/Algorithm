#pragma once
#include <vector>
#include <algorithm>
#include <climits>
#include <cmath>

using namespace std;

namespace Teammate {
    const int SIZE = 7;
    const int EMPTY = 0;
    const int AI = 1;
    const int ENEMY = -1;

    // 속도 문제로 팀원 AI는 Depth 3 고정
    const int MAX_DEPTH = 3;

    struct Move { int fromR, fromC, toR, toC; };

    class BacteriaAI {
    public:
        Move getBestMove(vector<vector<int>> board, int myStone) {
            vector<Move> moves = getAllMoves(board, myStone);
            if (moves.empty()) return { -1, -1, -1, -1 };
            orderMoves(board, moves, myStone, myStone);
            int bestScore = INT_MIN;
            Move bestMove = moves[0];
            for (Move move : moves) {
                vector<vector<int>> next = applyMove(board, move, myStone);
                int score = minimax(next, MAX_DEPTH - 1, false, myStone, INT_MIN, INT_MAX);
                if (score > bestScore) { bestScore = score; bestMove = move; }
            }
            return bestMove;
        }

    private:
        int minimax(vector<vector<int>> board, int depth, bool isMaxTurn, int myStone, int alpha, int beta) {
            if (depth == 0 || isGameOver(board)) return evaluate(board, myStone);
            int currentStone = isMaxTurn ? myStone : -myStone;
            vector<Move> moves = getAllMoves(board, currentStone);
            if (moves.empty()) return evaluate(board, myStone);
            orderMoves(board, moves, currentStone, myStone);

            if (isMaxTurn) {
                int best = INT_MIN;
                for (Move move : moves) {
                    vector<vector<int>> next = applyMove(board, move, currentStone);
                    int score = minimax(next, depth - 1, false, myStone, alpha, beta);
                    best = max(best, score); alpha = max(alpha, best);
                    if (beta <= alpha) break;
                }
                return best;
            }
            else {
                int best = INT_MAX;
                for (Move move : moves) {
                    vector<vector<int>> next = applyMove(board, move, currentStone);
                    int score = minimax(next, depth - 1, true, myStone, alpha, beta);
                    best = min(best, score); beta = min(beta, best);
                    if (beta <= alpha) break;
                }
                return best;
            }
        }

    public:
        static vector<Move> getAllMoves(const vector<vector<int>>& board, int stone) {
            vector<Move> moves;
            for (int r = 0; r < SIZE; r++) {
                for (int c = 0; c < SIZE; c++) {
                    if (board[r][c] != stone) continue;
                    for (int dr = -2; dr <= 2; dr++) {
                        for (int dc = -2; dc <= 2; dc++) {
                            if (dr == 0 && dc == 0) continue;
                            int nr = r + dr; int nc = c + dc;
                            if (!inRange(nr, nc)) continue;
                            if (board[nr][nc] != EMPTY) continue;
                            int dist = max(abs(dr), abs(dc));
                            if (dist == 1 || dist == 2) moves.push_back({ r, c, nr, nc });
                        }
                    }
                }
            }
            return moves;
        }

        static vector<vector<int>> applyMove(vector<vector<int>> board, Move move, int stone) {
            int dist = max(abs(move.fromR - move.toR), abs(move.fromC - move.toC));
            if (dist == 2) board[move.fromR][move.fromC] = EMPTY;
            board[move.toR][move.toC] = stone;
            for (int dr = -1; dr <= 1; dr++) {
                for (int dc = -1; dc <= 1; dc++) {
                    if (dr == 0 && dc == 0) continue;
                    int nr = move.toR + dr; int nc = move.toC + dc;
                    if (inRange(nr, nc) && board[nr][nc] == -stone) board[nr][nc] = stone;
                }
            }
            return board;
        }

        static bool isGameOver(const vector<vector<int>>& board) {
            if (countStone(board, EMPTY) == 0) return true;
            if (countStone(board, AI) == 0 || countStone(board, ENEMY) == 0) return true;
            return getAllMoves(board, AI).empty() && getAllMoves(board, ENEMY).empty();
        }

        static int countStone(const vector<vector<int>>& board, int stone) {
            int count = 0;
            for (int r = 0; r < SIZE; r++) {
                for (int c = 0; c < SIZE; c++) {
                    if (board[r][c] == stone) count++;
                }
            }
            return count;
        }

    private:
        int evaluate(const vector<vector<int>>& board, int myStone) {
            int enemyStone = -myStone;
            int myCount = countStone(board, myStone);
            int enemyCount = countStone(board, enemyStone);
            int totalStones = myCount + enemyCount;
            if (enemyCount == 0) return 1000000;
            if (myCount == 0) return -1000000;

            int myMoves = getAllMoves(board, myStone).size();
            int enemyMoves = getAllMoves(board, enemyStone).size();
            if (myMoves == 0 && enemyMoves > 0) return -900000;
            if (enemyMoves == 0 && myMoves > 0) return 900000;

            double myPercent = (double)myCount / totalStones;
            double enemyPercent = (double)enemyCount / totalStones;
            double progress = (double)totalStones / (SIZE * SIZE);

            int countScore = myCount - enemyCount;
            int mobilityScore = myMoves - enemyMoves;
            int cornerScore = countCorners(board, myStone) - countCorners(board, enemyStone);
            int edgeScore = countEdges(board, myStone) - countEdges(board, enemyStone);
            int frontierScore = countFrontier(board, enemyStone) - countFrontier(board, myStone);
            int attackScore = countImmediateGain(board, myStone) - countImmediateGain(board, enemyStone);

            double disadvantage = max(0.0, enemyPercent - myPercent);
            double advantage = max(0.0, myPercent - enemyPercent);

            double wCount = 80 + 120 * progress + 80 * advantage;
            double wMobility = 30 + 100 * (1.0 - progress) + 80 * disadvantage;
            double wDefense = 30 + 120 * disadvantage;
            double wCorner = 40 + 80 * disadvantage;
            double wEdge = 10 + 40 * progress;
            double wAttack = 30 + 100 * advantage + 80 * progress;
            double wFrontier = 15 + 100 * disadvantage;

            if (isDeadZone(board, myStone)) {
                wDefense *= 2.0; wMobility *= 1.7; wCorner *= 1.5; wCount *= 0.7; wAttack *= 0.6;
            }

            return (int)(wCount * countScore) + (int)(wMobility * mobilityScore) + (int)(wCorner * cornerScore) +
                (int)(wEdge * edgeScore) + (int)(wFrontier * frontierScore) + (int)(wAttack * attackScore);
        }

        bool isDeadZone(const vector<vector<int>>& board, int myStone) {
            int enemyStone = -myStone;
            int myCount = countStone(board, myStone);
            int enemyCount = countStone(board, enemyStone);
            int totalStones = myCount + enemyCount;
            if (myCount == 0) return true;
            int myMoves = getAllMoves(board, myStone).size();
            int enemyMoves = getAllMoves(board, enemyStone).size();
            if (myMoves == 0 && enemyMoves > 0) return true;

            double myPercent = (double)myCount / totalStones * 100.0;
            double progress = (double)totalStones / (SIZE * SIZE) * 100.0;

            if (progress < 35) return myPercent < 15 && myMoves <= 2;
            else if (progress < 70) return myPercent < 25 && myMoves <= 3;
            else return myPercent < 40;
        }

        int countImmediateGain(const vector<vector<int>>& board, int stone) {
            vector<Teammate::Move> moves = getAllMoves(board, stone);
            int bestGain = 0;
            for (Teammate::Move move : moves) {
                int gain = 0;
                for (int dr = -1; dr <= 1; dr++) {
                    for (int dc = -1; dc <= 1; dc++) {
                        if (dr == 0 && dc == 0) continue;
                        int nr = move.toR + dr; int nc = move.toC + dc;
                        if (inRange(nr, nc) && board[nr][nc] == -stone) gain++;
                    }
                }
                int dist = max(abs(move.fromR - move.toR), abs(move.fromC - move.toC));
                if (dist == 1) gain++;
                bestGain = max(bestGain, gain);
            }
            return bestGain;
        }

        void orderMoves(vector<vector<int>>& board, vector<Move>& moves, int currentStone, int myStone) {
            sort(moves.begin(), moves.end(), [&](Move a, Move b) {
                int scoreA = evaluate(applyMove(board, a, currentStone), myStone);
                int scoreB = evaluate(applyMove(board, b, currentStone), myStone);
                return (currentStone == myStone) ? (scoreA > scoreB) : (scoreA < scoreB);
                });
        }

        static int countCorners(const vector<vector<int>>& board, int stone) {
            int count = 0;
            if (board[0][0] == stone) count++; if (board[0][SIZE - 1] == stone) count++;
            if (board[SIZE - 1][0] == stone) count++; if (board[SIZE - 1][SIZE - 1] == stone) count++;
            return count;
        }

        static int countEdges(const vector<vector<int>>& board, int stone) {
            int count = 0;
            for (int i = 0; i < SIZE; i++) {
                if (board[0][i] == stone) count++; if (board[SIZE - 1][i] == stone) count++;
                if (board[i][0] == stone) count++; if (board[i][SIZE - 1] == stone) count++;
            }
            return count;
        }

        static int countFrontier(const vector<vector<int>>& board, int stone) {
            int count = 0;
            for (int r = 0; r < SIZE; r++) {
                for (int c = 0; c < SIZE; c++) {
                    if (board[r][c] != stone) continue;
                    bool nearEmpty = false;
                    for (int dr = -1; dr <= 1; dr++) {
                        for (int dc = -1; dc <= 1; dc++) {
                            if (dr == 0 && dc == 0) continue;
                            int nr = r + dr; int nc = c + dc;
                            if (inRange(nr, nc) && board[nr][nc] == EMPTY) { nearEmpty = true; break; }
                        }
                        if (nearEmpty) break;
                    }
                    if (nearEmpty) count++;
                }
            }
            return count;
        }

        static bool inRange(int r, int c) { return r >= 0 && r < SIZE && c >= 0 && c < SIZE; }
    };
} // end namespace Teammate