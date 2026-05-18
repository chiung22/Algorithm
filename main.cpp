#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <iomanip>

#include "ChiungEngine.h"
#include "TeammateEngine.h"

using namespace std;
using namespace std::chrono;

// =====================================================================
// [설정 스위치] 중계 모드 vs 초고속 모드
// =====================================================================
// true로 설정하면 보드판과 턴 진행 상황이 콘솔에 화려하게 중계됩니다. (1~5판 관전용 추천)
// false로 설정하면 화면 출력을 끄고 빛의 속도로 엑셀 지표만 추출합니다. (100~1000판용 추천)
const bool SHOW_BROADCAST = true;

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
// [실시간 보드판 중계 함수]
// =====================================================================
void printBoard(const Chiung::Bitboard& b) {
    cout << "\n    0 1 2 3 4 5 6  (열)\n";
    cout << "  +---------------+\n";
    for (int r = 0; r < 7; r++) {
        cout << r << " | ";
        for (int c = 0; c < 7; c++) {
            int idx = r * 7 + c;
            if (b.p1 & (1ULL << idx)) cout << "● ";
            else if (b.p2 & (1ULL << idx)) cout << "○ ";
            else cout << ". ";
        }
        cout << "|\n";
    }
    cout << "  +---------------+\n";
    cout << "📊 현재 스코어 -> [치웅 V10(●): " << Chiung::popcount64(b.p1)
        << "개] vs [팀원 V2(○): " << Chiung::popcount64(b.p2) << "개]\n";
    cout << "---------------------------------------------------------\n\n";
}

int main() {
    Chiung::AtaxxEngine::initMasks();
    Teammate::initEngine();

    ofstream csv("Algorithm_Metrics_FairFight.csv");
    // [수정] 팀원_최저점유율_% 항목 추가
    csv << "게임번호,시간제한(초),승리자,치웅_최저점유율_%,팀원_최저점유율_%,치웅_평균시간_ms,팀원_평균시간_ms,치웅_NPS,팀원_NPS,총턴수\n";

    cout << "=========================================================\n";
    cout << " 🚀 완전 공정 대결: 시간 제한 x 멀티스레드 x 무한 뎁스\n";
    cout << "=========================================================\n";
    cout << "- P1: 치웅 엔진 V10 (동적 휴리스틱 / 킬각+코너 장착)\n";
    cout << "- P2: 팀원 엔진 V2 (정적 휴리스틱)\n";
    if (SHOW_BROADCAST) cout << "📡 [실시간 중계 모드 ON] 경기 과정을 화면에 출력합니다.\n\n";
    else cout << "⚡ [초고속 무음 모드 ON] 과정 생략 후 데이터만 추출합니다.\n\n";

    int gameID = 1;

    // [설정] 판수 및 시간제한
    const int ITERATIONS = 100;
    const double TIME_LIMIT = 2; // 턴당 제한시간 (빠른 테스트를 위해 0.05초 권장)

    cout << "[제한시간 " << TIME_LIMIT << "초] " << ITERATIONS << "판 시뮬레이션 진행 중...\n";

    int p1Wins = 0, p2Wins = 0, draws = 0;

    for (int iter = 1; iter <= ITERATIONS; iter++) {
        fill(Chiung::TT_Cache.begin(), Chiung::TT_Cache.end(), Chiung::TTEntry{ 0, -1, 0, Chiung::EXACT });
        fill(Teammate::TT_Cache.begin(), Teammate::TT_Cache.end(), Teammate::TTEntry{ 0, -1, Teammate::EXACT, 0 });

        Chiung::Bitboard board;
        board.init();

        if (SHOW_BROADCAST) {
            cout << "\n🎯 === [" << iter << "번째 판 시작] === 🎯\n";
            printBoard(board);
        }

        double p1TotalTimeMs = 0.0, p2TotalTimeMs = 0.0;
        int p1MovesCount = 0, p2MovesCount = 0;
        int turns = 0;

        // [수정] 양측 모두의 최저 점유율 추적
        double lowest_p1_percent = 100.0;
        double lowest_p2_percent = 100.0;

        long long p1TotalNodesAccum = 0;
        long long p2TotalNodesAccum = 0;

        while (!board.isGameOver() && turns < 200) {
            int p1Count = Chiung::popcount64(board.p1);
            int p2Count = Chiung::popcount64(board.p2);
            int totalCount = p1Count + p2Count;

            if (totalCount > 4) {
                double currentP1Percent = ((double)p1Count / totalCount) * 100.0;
                double currentP2Percent = ((double)p2Count / totalCount) * 100.0;
                if (currentP1Percent < lowest_p1_percent) lowest_p1_percent = currentP1Percent;
                // [수정] 팀원의 최저 점유율 갱신
                if (currentP2Percent < lowest_p2_percent) lowest_p2_percent = currentP2Percent;
            }

            int currColor = (turns % 2 == 0 ? Chiung::PLAYER1 : Chiung::PLAYER2);

            if (currColor == Chiung::PLAYER1) {
                if (SHOW_BROADCAST) cout << "🧠 [치웅 V10 (흑●) 생각 중...]\n";

                auto start = high_resolution_clock::now();
                Chiung::Move bestMove = Chiung::AtaxxEngine::getBestMoveTimeLimited(board, Chiung::PLAYER1, TIME_LIMIT);
                auto end = high_resolution_clock::now();

                double elapsed = duration<double>(end - start).count();
                p1TotalTimeMs += elapsed * 1000.0;
                long long turnNodes = Chiung::AtaxxEngine::totalGameNodes.load();
                p1TotalNodesAccum += turnNodes;
                p1MovesCount++;

                if (bestMove.to != -1) {
                    if (SHOW_BROADCAST) {
                        int r2 = bestMove.to / 7, c2 = bestMove.to % 7;
                        if (bestMove.isClone) cout << "➡️ ⚡ 치웅 AI: (" << r2 << ", " << c2 << ") 로 1칸 [복제] 완료!\n";
                        else cout << "➡️ 🔥 치웅 AI: (" << bestMove.from / 7 << ", " << bestMove.from % 7 << ") 에서 (" << r2 << ", " << c2 << ") 로 2칸 [점프] 완료!\n";
                        long long nps = (elapsed > 0) ? (long long)(turnNodes / elapsed) : 0;
                        cout << "   ✨ [지표] 소요시간: " << fixed << setprecision(3) << elapsed << "초 | 탐색속도: " << nps << " NPS\n";
                    }
                    board = Chiung::AtaxxEngine::applyMove(board, bestMove, Chiung::PLAYER1);
                }
            }
            else {
                if (SHOW_BROADCAST) cout << "🧠 [팀원 V2 (백○) 생각 중...]\n";
                Teammate::Bitboard tBoard = convertToTeammateBoard(board);

                auto start = high_resolution_clock::now();
                Teammate::Move tmMove = Teammate::AtaxxEngine::getBestMoveTimeLimited(tBoard, Teammate::PLAYER2, TIME_LIMIT, 1, 2, 3);
                auto end = high_resolution_clock::now();

                double elapsed = duration<double>(end - start).count();
                p2TotalTimeMs += elapsed * 1000.0;
                long long turnNodes = Teammate::AtaxxEngine::totalNodes.load();
                p2TotalNodesAccum += turnNodes;
                p2MovesCount++;

                if (tmMove.to != -1) {
                    if (SHOW_BROADCAST) {
                        int r2 = tmMove.to / 7, c2 = tmMove.to % 7;
                        if (tmMove.isClone) cout << "➡️ ⚡ 팀원 AI: (" << r2 << ", " << c2 << ") 로 1칸 [복제] 완료!\n";
                        else cout << "➡️ 🔥 팀원 AI: (" << tmMove.from / 7 << ", " << tmMove.from % 7 << ") 에서 (" << r2 << ", " << c2 << ") 로 2칸 [점프] 완료!\n";
                        long long nps = (elapsed > 0) ? (long long)(turnNodes / elapsed) : 0;
                        cout << "   ✨ [지표] 소요시간: " << fixed << setprecision(3) << elapsed << "초 | 탐색속도: " << nps << " NPS\n";
                    }
                    Chiung::Move cMove = { tmMove.from, tmMove.to, tmMove.isClone, 0, 0 };
                    board = Chiung::AtaxxEngine::applyMove(board, cMove, Chiung::PLAYER2);
                }
            }

            if (SHOW_BROADCAST) printBoard(board);
            turns++;
        }

        int winner = board.getWinner();
        if (winner == Chiung::PLAYER1) p1Wins++;
        else if (winner == Chiung::PLAYER2) p2Wins++;
        else draws++;

        double p1AvgTime = (p1MovesCount > 0) ? (p1TotalTimeMs / p1MovesCount) : 0.0;
        double p2AvgTime = (p2MovesCount > 0) ? (p2TotalTimeMs / p2MovesCount) : 0.0;

        long long p1Nps = (p1TotalTimeMs > 0) ? (long long)(p1TotalNodesAccum / (p1TotalTimeMs / 1000.0)) : 0;
        long long p2Nps = (p2TotalTimeMs > 0) ? (long long)(p2TotalNodesAccum / (p2TotalTimeMs / 1000.0)) : 0;

        // [수정] CSV에 팀원 최저 점유율 데이터 삽입
        csv << gameID++ << "," << TIME_LIMIT << "," << winner << "," << lowest_p1_percent << "," << lowest_p2_percent << ","
            << p1AvgTime << "," << p2AvgTime << "," << p1Nps << "," << p2Nps << "," << turns << "\n";

        if (!SHOW_BROADCAST && iter % 10 == 0) {
            cout << "  -> " << iter << "판 완료... [스코어] 치웅: " << p1Wins << "승 | 태완: " << p2Wins << "승 | 무승부: " << draws << "\n";
        }
        else if (SHOW_BROADCAST) {
            cout << "🏁 " << iter << "번째 판 종료! 승자: " << (winner == Chiung::PLAYER1 ? "치웅(P1)" : (winner == Chiung::PLAYER2 ? "팀원(P2)" : "무승부")) << "\n";
        }
    }

    cout << "\n🔥 제한시간 " << TIME_LIMIT << "초 공정 대결 최종 결과 | 치웅 승: " << p1Wins << " / 팀원 승: " << p2Wins << " / 무승부: " << draws << "\n\n";

    csv.close();
    cout << "✅ 시뮬레이션 끝! 모든 지표가 'Algorithm_Metrics_FairFight.csv' 파일에 저장되었습니다.\n";
    return 0;
}