#include <iostream>
#include <fstream>
#include <string>
#include <chrono>

#include "ChiungEngine.h"
#include "TeammateEngine.h"

using namespace std;
using namespace std::chrono;

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

int main() {
    Chiung::AtaxxEngine::initMasks();
    Teammate::initEngine();

    ofstream csv("Algorithm_Metrics_1000_Games.csv");
    csv << "게임번호,시간제한(초),승리자,치웅_최저점유율_%,치웅_평균시간_ms,팀원_평균시간_ms,치웅_NPS,팀원_NPS,총턴수\n";

    cout << "=========================================================\n";
    cout << " 🚀 완전 공정 대결: 1.0초 제한 x 멀티스레드 x 무한 뎁스\n";
    cout << "=========================================================\n";
    cout << "- P1: 치웅 엔진 (GEMINI 휴리스틱)\n";
    cout << "- P2: 팀원 엔진 (GPT 휴리스틱)\n\n";

    int gameID = 1;

    // 판수 설정 부분 
    const int ITERATIONS = 1;
    // 판수 설정 부분
    const double TIME_LIMIT = 1.0; // 턴당 제한시간 1초

    cout << "[제한시간 " << TIME_LIMIT << "초] " << ITERATIONS << "판 시뮬레이션 진행 중...\n";

    int p1Wins = 0, p2Wins = 0, draws = 0;

    for (int iter = 1; iter <= ITERATIONS; iter++) {
        fill(Chiung::TT_Cache.begin(), Chiung::TT_Cache.end(), Chiung::TTEntry{ 0, -1, 0, Chiung::EXACT });
        fill(Teammate::TT_Cache.begin(), Teammate::TT_Cache.end(), Teammate::TTEntry{ 0, -1, Teammate::EXACT, 0 });

        Chiung::Bitboard board;
        board.init();

        double p1TotalTimeMs = 0.0, p2TotalTimeMs = 0.0;
        int p1MovesCount = 0, p2MovesCount = 0;
        int turns = 0;
        double lowest_p1_percent = 100.0;

        while (!board.isGameOver() && turns < 200) {
            int p1Count = Chiung::popcount64(board.p1);
            int totalCount = p1Count + Chiung::popcount64(board.p2);
            if (totalCount > 4) {
                double currentPercent = ((double)p1Count / totalCount) * 100.0;
                if (currentPercent < lowest_p1_percent) lowest_p1_percent = currentPercent;
            }

            int currColor = (turns % 2 == 0 ? Chiung::PLAYER1 : Chiung::PLAYER2);

            if (currColor == Chiung::PLAYER1) {
                auto start = high_resolution_clock::now();
                // 치웅 엔진 1.0초 멀티스레드 탐색
                Chiung::Move bestMove = Chiung::AtaxxEngine::getBestMoveTimeLimited(board, Chiung::PLAYER1, TIME_LIMIT);
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
                // [핵심 변경] 팀원 엔진도 1.0초 멀티스레드 탐색 적용
                Teammate::Move tmMove = Teammate::AtaxxEngine::getBestMoveTimeLimited(tBoard, Teammate::PLAYER2, TIME_LIMIT, 1, 2, 3);
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

        int winner = board.getWinner();
        if (winner == Chiung::PLAYER1) p1Wins++;
        else if (winner == Chiung::PLAYER2) p2Wins++;
        else draws++;

        double p1AvgTime = (p1MovesCount > 0) ? (p1TotalTimeMs / p1MovesCount) : 0.0;
        double p2AvgTime = (p2MovesCount > 0) ? (p2TotalTimeMs / p2MovesCount) : 0.0;
        long long p1Nps = (p1TotalTimeMs > 0) ? (long long)(Chiung::AtaxxEngine::totalGameNodes / (p1TotalTimeMs / 1000.0)) : 0;
        long long p2Nps = (p2TotalTimeMs > 0) ? (long long)(Teammate::AtaxxEngine::totalNodes / (p2TotalTimeMs / 1000.0)) : 0;

        csv << gameID++ << "," << TIME_LIMIT << "," << winner << "," << lowest_p1_percent << ","
            << p1AvgTime << "," << p2AvgTime << "," << p1Nps << "," << p2Nps << "," << turns << "\n";

        cout << "  -> " << iter << "판 완료... (현재 치웅 승: " << p1Wins << ")\n";
    }

    cout << "🔥 제한시간 " << TIME_LIMIT << "초 공정 대결 최종 결과 | 치웅 승: " << p1Wins << " / 팀원 승: " << p2Wins << " / 무승부: " << draws << "\n\n";

    csv.close();
    cout << "✅ 시뮬레이션 끝! 모든 지표가 'Algorithm_Metrics_FairFight.csv' 파일에 저장되었습니다.\n";
    return 0;
}