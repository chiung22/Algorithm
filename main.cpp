#include <iostream>
#include <fstream>
#include <string>
#include <chrono>

// 분리된 헤더 파일들 불러오기 (이 파일들은 수정할 필요 없습니다!)
#include "ChiungEngine.h"
#include "TeammateEngine.h"

using namespace std;
using namespace std::chrono;

// =====================================================================
// [보드판 상태 변환 어댑터]
// =====================================================================
Teammate::Bitboard convertToTeammateBoard(const Chiung::Bitboard& cb) {
    Teammate::Bitboard tb;
    tb.p1 = cb.p1;
    tb.p2 = cb.p2;
    tb.hashKey = 0;
    uint64_t temp1 = tb.p1;
    while (temp1) { int idx = Teammate::ctz64(temp1); tb.hashKey ^= Teammate::ZOBRIST_TABLE[idx][0]; temp1 &= temp1 - 1; }
    uint64_t temp2 = tb.p2;
    while (temp2) { int idx = Teammate::ctz64(temp2); tb.hashKey ^= Teammate::ZOBRIST_TABLE[idx][1]; temp2 &= temp2 - 1; }
    tb.hashKey ^= Teammate::ZOBRIST_TURN;
    return tb;
}

// =====================================================================
// [메인 함수: 1,000판 무음(Silent) 초고속 시뮬레이션]
// =====================================================================
int main() {
    // 1. 엔진 초기화
    Chiung::AtaxxEngine::initMasks();
    Teammate::initEngine();

    // 2. CSV 파일 세팅 (지표 7가지 기록)
    ofstream csv("Algorithm_Metrics_1000_Games.csv");
    csv << "GameID,Test_Depth,Winner,Lowest_P1_Percent,P1_TT_Hit_Rate_%,P1_Avg_Time_ms,P2_Avg_Time_ms,P1_NPS,P2_NPS,TotalTurns\n";

    cout << "=========================================================\n";
    cout << " 🚀 1,000판 알고리즘 성능 지표 자동 추출 시작\n";
    cout << "=========================================================\n";
    cout << "- P1: 치웅 V9 (동적 휴리스틱)\n";
    cout << "- P2: 팀원 V2 (정적 휴리스틱)\n";

    int gameID = 1;

    // [핵심] 시뮬레이션 판 수 설정: 1,000판
    const int ITERATIONS = 1000;

    // Depth 2, 3, 4를 순차적으로 테스트
    for (int test_depth = 2; test_depth <= 4; test_depth++) {
        cout << "[Depth " << test_depth << "] 1,000판 시뮬레이션 진행 중...\n";

        int p1Wins = 0, p2Wins = 0, draws = 0;

        for (int iter = 1; iter <= ITERATIONS; iter++) {
            // 게임 캐시 초기화
            fill(Chiung::TT_Cache.begin(), Chiung::TT_Cache.end(), Chiung::TTEntry{ 0, 0, 0, 0 });
            fill(Teammate::TT_Cache.begin(), Teammate::TT_Cache.end(), Teammate::TTEntry{ 0, -1, Teammate::EXACT, 0 });

            Chiung::Bitboard board;
            board.init();

            double p1TotalTimeMs = 0.0, p2TotalTimeMs = 0.0;
            int p1MovesCount = 0, p2MovesCount = 0;
            int turns = 0;
            double lowest_p1_percent = 100.0;

            // 단일 게임 루프 (cout 없음, 오직 연산만)
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
                    // [치웅 엔진 턴] FixedDepth로 팀원과 체급을 맞추거나 올려서 테스트
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
                    // [팀원 엔진 턴] 고정 Depth 3으로 탐색
                    Teammate::Move tmMove = Teammate::GPTAI::getBestMove(tBoard, Teammate::PLAYER2, 3, 1, 2, 3);
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

            // 1판 종료 시 통계 취합 및 CSV 한 줄 작성
            int winner = board.getWinner();
            if (winner == Chiung::PLAYER1) p1Wins++;
            else if (winner == Chiung::PLAYER2) p2Wins++;
            else draws++;

            double p1AvgTime = (p1MovesCount > 0) ? (p1TotalTimeMs / p1MovesCount) : 0.0;
            double p2AvgTime = (p2MovesCount > 0) ? (p2TotalTimeMs / p2MovesCount) : 0.0;
            double p1TtHitRate = (Chiung::AtaxxEngine::ttLookups > 0) ? ((double)Chiung::AtaxxEngine::ttHits / Chiung::AtaxxEngine::ttLookups) * 100.0 : 0.0;
            long long p1Nps = (p1TotalTimeMs > 0) ? (long long)(Chiung::AtaxxEngine::totalGameNodes / (p1TotalTimeMs / 1000.0)) : 0;
            long long p2Nps = (p2TotalTimeMs > 0) ? (long long)(Teammate::AtaxxEngine::totalNodes / (p2TotalTimeMs / 1000.0)) : 0;

            csv << gameID++ << "," << test_depth << ","
                << winner << "," << lowest_p1_percent << "," << p1TtHitRate << ","
                << p1AvgTime << "," << p2AvgTime << "," << p1Nps << "," << p2Nps << "," << turns << "\n";

            // [사용자 편의 기능] 100판마다 콘솔에 진행 상황 한 줄씩 출력 (안 그러면 멈춘 줄 앎)
            if (iter % 100 == 0) {
                cout << "  -> " << iter << "판 완료... (현재 치웅 승리: " << p1Wins << ")\n";
            }
        }
        cout << "🔥 Depth " << test_depth << " 최종 결과 | 치웅 승: " << p1Wins << " / 팀원 승: " << p2Wins << " / 무승부: " << draws << "\n\n";
    }

    csv.close();
    cout << "✅ 1,000판 시뮬레이션 끝! 모든 지표가 'Algorithm_Metrics_1000_Games.csv' 파일에 저장되었습니다.\n";
    return 0;
}