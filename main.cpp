#include <iostream>
#include <fstream>
#include <string>
#include <random>

// [파일 분리] 직접 만든 두 개의 헤더 파일을 불러옵니다.
#include "ChiungEngine.h"
#include "TeammateEngine.h"

using namespace std;
using namespace std::chrono;

// =====================================================================
// [통합 아레나 포맷 어댑터]
// =====================================================================
vector<vector<int>> convertBitboardToTeammate(const Bitboard& b, int teammateColor) {
    vector<vector<int>> tb(7, vector<int>(7, 0));
    for (int i = 0; i < 49; i++) {
        int r = i / 7; int c = i % 7;
        if ((b.p1 >> i) & 1) tb[r][c] = (teammateColor == PLAYER1) ? 1 : -1;
        else if ((b.p2 >> i) & 1) tb[r][c] = (teammateColor == PLAYER2) ? 1 : -1;
    }
    return tb;
}

Move convertTeammateMoveToChiung(const Teammate::Move& tm) {
    if (tm.fromR == -1) return { -1, -1, false, 0, 0 };
    int fromIdx = tm.fromR * 7 + tm.fromC;
    int toIdx = tm.toR * 7 + tm.toC;
    bool isClone = (max(abs(tm.fromR - tm.toR), abs(tm.fromC - tm.toC)) <= 1);
    return { fromIdx, toIdx, isClone, 0, 0 };
}

// =====================================================================
// [메인 함수: 시뮬레이션 및 데이터 추출]
// =====================================================================
int main() {
    mt19937_64 rng(12345);
    for (int i = 0; i < 49; i++) {
        ZOBRIST_TABLE[i][0] = rng(); ZOBRIST_TABLE[i][1] = rng();
        for (int r = -2; r <= 2; r++) {
            for (int c = -2; c <= 2; c++) {
                int nr = (i / 7) + r, nc = (i % 7) + c;
                if (nr >= 0 && nr < 7 && nc >= 0 && nc < 7) {
                    int nidx = nr * 7 + nc;
                    if (abs(r) <= 1 && abs(c) <= 1) ADJACENT_MASK[i] |= (1ULL << nidx);
                    else JUMP_MASK[i] |= (1ULL << nidx);
                }
            }
        }
    }

    // 데이터 추출용 CSV 파일 세팅
    ofstream csv("Algorithm_Comparison_Metrics.csv");
    csv << "GameID,P1_Algorithm,P2_Algorithm,P1_Depth,P2_Depth,Winner,Lowest_P1_%,P1_TT_Hit_Rate_%,P1_Avg_Time_ms,P2_Avg_Time_ms,P1_Avg_Nodes,TotalTurns\n";

    cout << "=== 알고리즘 성능 비교 대규모 시뮬레이션 시작 ===\n";
    cout << "- P1: 치웅 AI (Bitboard, 동적 휴리스틱)\n";
    cout << "- P2: 팀원 AI (2D Array, 정적 휴리스틱)\n\n";

    int gameID = 1;
    mt19937 shuffle_rng(42);
    Teammate::BacteriaAI teammateEngine;

    // 실험: 치웅 AI의 탐색 깊이(Depth)를 2부터 4까지 올려가며 팀원 AI(Depth 3)와 맞대결
    for (int test_depth = 2; test_depth <= 4; test_depth++) {
        cout << "[탐색 깊이(Depth) " << test_depth << " 시뮬레이션 진행 중...]\n";

        int p1Wins = 0, p2Wins = 0, draws = 0;

        // 각 Depth마다 10판씩 진행하여 통계 확보
        for (int iter = 1; iter <= 10; iter++) {
            fill(TT_Cache.begin(), TT_Cache.end(), TTEntry{ 0, 0, 0, 0 });
            Bitboard board; board.init();

            // P1(치웅) 성능 추적 변수 초기화
            ChiungEngine::ttHits = 0;
            ChiungEngine::ttLookups = 0;
            ChiungEngine::totalGameNodes = 0;
            double p1TotalTimeMs = 0.0;
            int p1MovesCount = 0;

            // P2(팀원) 성능 추적 변수 초기화
            double p2TotalTimeMs = 0.0;
            int p2MovesCount = 0;

            int turns = 0;
            double lowest_p1_percent = 100.0;

            while (!board.isGameOver() && turns < 200) {
                // [데스존 추적] 매 턴 P1의 최저 점유율 갱신
                int p1Count = __builtin_popcountll(board.p1);
                int totalCount = p1Count + __builtin_popcountll(board.p2);
                if (totalCount > 4) {
                    double currentPercent = ((double)p1Count / totalCount) * 100.0;
                    if (currentPercent < lowest_p1_percent) lowest_p1_percent = currentPercent;
                }

                int currColor = (turns % 2 == 0 ? PLAYER1 : PLAYER2);

                if (currColor == PLAYER1) {
                    // P1 (치웅 AI) 턴: Bitboard + Zobrist 연산
                    vector<Move> mvs = ChiungEngine::getValidMoves(board, PLAYER1);
                    if (!mvs.empty()) {
                        shuffle(mvs.begin(), mvs.end(), shuffle_rng);
                        Move bestMove = mvs[0];
                        int bestScore = -1000000;

                        auto start_time = high_resolution_clock::now();
                        for (auto& m : mvs) {
                            Bitboard nextB = ChiungEngine::applyMove(board, m, PLAYER1);
                            int score = ChiungEngine::minimax(nextB, test_depth - 1, -1000000, 1000000, false, PLAYER1);
                            if (score > bestScore) { bestScore = score; bestMove = m; }
                        }
                        auto end_time = high_resolution_clock::now();

                        p1TotalTimeMs += duration<double, std::milli>(end_time - start_time).count();
                        p1MovesCount++;

                        if (bestMove.to != -1) {
                            board = ChiungEngine::applyMove(board, bestMove, PLAYER1);
                        }
                    }
                }
                else {
                    // P2 (팀원 AI) 턴: 2D Array 연산
                    vector<vector<int>> tmBoard = convertBitboardToTeammate(board, PLAYER2);

                    auto start_time = high_resolution_clock::now();
                    Teammate::Move tmMove = teammateEngine.getBestMove(tmBoard, 1);
                    auto end_time = high_resolution_clock::now();

                    p2TotalTimeMs += duration<double, std::milli>(end_time - start_time).count();
                    p2MovesCount++;

                    if (tmMove.toR != -1) {
                        Move convertedMove = convertTeammateMoveToChiung(tmMove);
                        board = ChiungEngine::applyMove(board, convertedMove, PLAYER2);
                    }
                }
                turns++;
            }

            // 통계 계산
            double p1AvgTime = (p1MovesCount > 0) ? (p1TotalTimeMs / p1MovesCount) : 0.0;
            double p2AvgTime = (p2MovesCount > 0) ? (p2TotalTimeMs / p2MovesCount) : 0.0;
            double p1AvgNodes = (p1MovesCount > 0) ? ((double)ChiungEngine::totalGameNodes / p1MovesCount) : 0.0;
            double p1TtHitRate = (ChiungEngine::ttLookups > 0) ? ((double)ChiungEngine::ttHits / ChiungEngine::ttLookups) * 100.0 : 0.0;

            int winner = board.getWinner();

            // CSV 데이터 출력
            csv << gameID++ << ",Chiung_V9,Teammate_2D," << test_depth << ",3,"
                << winner << "," << lowest_p1_percent << "," << p1TtHitRate << ","
                << p1AvgTime << "," << p2AvgTime << "," << p1AvgNodes << "," << turns << "\n";

            if (winner == PLAYER1) p1Wins++;
            else if (winner == PLAYER2) p2Wins++;
            else draws++;
        }
        cout << "-> Depth " << test_depth << " 결과 | 치웅 승: " << p1Wins << " / 팀원 승: " << p2Wins << " / 무승부: " << draws << "\n";
    }

    csv.close();
    cout << "\n✅ 시뮬레이션 완료! 'Algorithm_Comparison_Metrics.csv' 파일이 생성되었습니다.\n";
    return 0;
}