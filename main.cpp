#include <iostream>
#include <fstream>
#include <string>
#include <chrono>

// 분리된 헤더 파일들 불러오기
#include "ChiungEngine.h"
#include "TeammateEngine.h"

using namespace std;
using namespace std::chrono;

// =====================================================================
// [보드판 상태 변환 어댑터]
// 치웅님의 Bitboard 데이터를 팀원의 Bitboard 데이터로 변환합니다.
// =====================================================================
Teammate::Bitboard convertToTeammateBoard(const Chiung::Bitboard& cb) {
    Teammate::Bitboard tb;
    tb.p1 = cb.p1;
    tb.p2 = cb.p2;
    // 해시값 재조립
    tb.hashKey = 0;
    uint64_t temp1 = tb.p1;
    while (temp1) { int idx = Teammate::ctz64(temp1); tb.hashKey ^= Teammate::ZOBRIST_TABLE[idx][0]; temp1 &= temp1 - 1; }
    uint64_t temp2 = tb.p2;
    while (temp2) { int idx = Teammate::ctz64(temp2); tb.hashKey ^= Teammate::ZOBRIST_TABLE[idx][1]; temp2 &= temp2 - 1; }
    tb.hashKey ^= Teammate::ZOBRIST_TURN;
    return tb;
}

// =====================================================================
// [메인 함수: 무음(Silent) 초고속 시뮬레이션 및 데이터 추출]
// =====================================================================
int main() {
    // 1. 양 진영 엔진 초기화 (각자의 네임스페이스 마스크 셋업)
    Chiung::AtaxxEngine::initMasks();
    Teammate::initEngine();

    // 2. CSV 파일 세팅
    ofstream csv("Algorithm_Metrics_Result.csv");
    csv << "GameID,P1_Algo,P2_Algo,Test_Depth,Winner,Lowest_P1_Percent,P1_TT_Hit_Rate,P1_Avg_Time_ms,P2_Avg_Time_ms,P1_NPS,P2_NPS,TotalTurns\n";

    cout << "=========================================================\n";
    cout << " 📊 알고리즘 성능 지표 추출 시뮬레이션 시작 (초고속 모드)\n";
    cout << "=========================================================\n";
    cout << "- P1: 치웅 V9 (동적 휴리스틱)\n";
    cout << "- P2: 팀원 AI (정적 휴리스틱)\n\n";

    int gameID = 1;
    const int ITERATIONS = 10; // 각 깊이별로 10판씩 진행

    // Depth 3부터 5까지 테스트 (팀원 코드도 비트보드라 속도가 빨라져서 5까지 가능합니다)
    for (int test_depth = 3; test_depth <= 5; test_depth++) {
        cout << "[탐색 깊이(Depth) " << test_depth << " 시뮬레이션 진행 중...] ";

        int p1Wins = 0, p2Wins = 0, draws = 0;

        for (int iter = 0; iter < ITERATIONS; iter++) {
            // 캐시 초기화
            fill(Chiung::TT_Cache.begin(), Chiung::TT_Cache.end(), Chiung::TTEntry{ 0, 0, 0, 0 });
            fill(Teammate::TT_Cache.begin(), Teammate::TT_Cache.end(), Teammate::TTEntry{ 0, -1, Teammate::EXACT, 0 });

            Chiung::Bitboard board;
            board.init();

            double p1TotalTimeMs = 0.0, p2TotalTimeMs = 0.0;
            int p1MovesCount = 0, p2MovesCount = 0;
            int turns = 0;
            double lowest_p1_percent = 100.0;

            while (!board.isGameOver() && turns < 200) {
                // 데스존(최저 점유율) 기록
                int p1Count = __builtin_popcountll(board.p1);
                int totalCount = p1Count + __builtin_popcountll(board.p2);
                if (totalCount > 4) {
                    double currentPercent = ((double)p1Count / totalCount) * 100.0;
                    if (currentPercent < lowest_p1_percent) lowest_p1_percent = currentPercent;
                }

                int currColor = (turns % 2 == 0 ? Chiung::PLAYER1 : Chiung::PLAYER2);

                if (currColor == Chiung::PLAYER1) {
                    auto start = high_resolution_clock::now();
                    Chiung::Move bestMove = Chiung::AtaxxEngine::getBestMoveFixedDepth(board, Chiung::PLAYER1, test_depth);
                    auto end = high_resolution_clock::now();

                    p1TotalTimeMs += duration<double, std::milli>(end - start).count();
                    p1MovesCount++;

                    if (bestMove.to != -1) {
                        board = Chiung::AtaxxEngine::applyMove(board, bestMove, Chiung::PLAYER1);
                    }
                }
                else {
                    Teammate::Bitboard tBoard = convertToTeammateBoard(board);

                    auto start = high_resolution_clock::now();
                    Teammate::Move tmMove = Teammate::GPTAI::getBestMove(tBoard, Teammate::PLAYER2, test_depth, 1, 2, 3);
                    auto end = high_resolution_clock::now();

                    p2TotalTimeMs += duration<double, std::milli>(end - start).count();
                    p2MovesCount++;

                    if (tmMove.to != -1) {
                        Chiung::Move cMove = { tmMove.from, tmMove.to, tmMove.isClone, 0, 0 };
                        board = Chiung::AtaxxEngine::applyMove(board, cMove, Chiung::PLAYER2);
                    }
                }
                turns++;
            }

            // 통계 취합
            int winner = board.getWinner();
            if (winner == Chiung::PLAYER1) p1Wins++;
            else if (winner == Chiung::PLAYER2) p2Wins++;
            else draws++;

            double p1AvgTime = (p1MovesCount > 0) ? (p1TotalTimeMs / p1MovesCount) : 0.0;
            double p2AvgTime = (p2MovesCount > 0) ? (p2TotalTimeMs / p2MovesCount) : 0.0;
            double p1TtHitRate = (Chiung::AtaxxEngine::ttLookups > 0) ? ((double)Chiung::AtaxxEngine::ttHits / Chiung::AtaxxEngine::ttLookups) * 100.0 : 0.0;
            long long p1Nps = (p1TotalTimeMs > 0) ? (long long)(Chiung::AtaxxEngine::totalGameNodes / (p1TotalTimeMs / 1000.0)) : 0;
            long long p2Nps = (p2TotalTimeMs > 0) ? (long long)(Teammate::AtaxxEngine::totalNodes / (p2TotalTimeMs / 1000.0)) : 0;

            csv << gameID++ << ",Chiung_V9,Teammate_V2," << test_depth << "," << test_depth << ","
                << winner << "," << lowest_p1_percent << "," << p1TtHitRate << ","
                << p1AvgTime << "," << p2AvgTime << "," << p1Nps << "," << p2Nps << "," << turns << "\n";
        }
        cout << "완료! (치웅 승: " << p1Wins << " / 팀원 승: " << p2Wins << " / 무승부: " << draws << ")\n";
    }

    csv.close();
    cout << "\n✅ 시뮬레이션 끝! 모든 지표가 'Algorithm_Metrics_Result.csv' 파일에 저장되었습니다.\n";
    return 0;
}