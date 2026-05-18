#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <iomanip>

#include "ChiungEngine.h"
#include "TeammateEngine.h"

using namespace std;
using namespace std::chrono;

const bool SHOW_BROADCAST = false; // true: 관전 모드, false: 데이터 추출 모드

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
    cout << "📊 현재 스코어 -> [치웅 V10.1(●): " << Chiung::popcount64(b.p1)
        << "개] vs [팀원 V2(○): " << Chiung::popcount64(b.p2) << "개]\n";
    cout << "---------------------------------------------------------\n\n";
}

// [추가 기능] 파일 덮어쓰기 방지를 위한 자동 넘버링 탐색 함수
string getNextFilename(const string& baseName, const string& extension) {
    int counter = 1;
    string filename;
    while (true) {
        filename = baseName + "_" + to_string(counter) + extension;
        ifstream f(filename.c_str());
        if (!f.good()) {
            break; // 해당 번호의 파일이 존재하지 않으면 루프 탈출 (이 이름으로 생성)
        }
        counter++;
    }
    return filename;
}

int main() {
    Chiung::AtaxxEngine::initMasks();
    Teammate::initEngine();

    // [수정] 고정된 파일명 대신 자동 넘버링 생성 함수를 호출하여 파일명 결정
    string outFileName = getNextFilename("Algorithm_Metrics_FairFight", ".csv");
    ofstream csv(outFileName);

    csv << "게임번호,시간제한(초),승리자,치웅_최저점유율_%,팀원_최저점유율_%,치웅_평균시간_ms,팀원_평균시간_ms,치웅_NPS,팀원_NPS,총턴수\n";

    cout << "=========================================================\n";
    cout << " 🚀 GEMINI VS GPT 대결\n";
    cout << "=========================================================\n";
    cout << "- P1: 치웅 엔진 V10.1\n";
    cout << "- P2: 팀원 엔진 V2\n";
    cout << "💾 저장될 파일명: " << outFileName << "\n\n";

    int gameID = 1;
    const int ITERATIONS = 100;
    const double TIME_LIMIT = 0.05;

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

        double lowest_p1_percent = 100.0;
        double lowest_p2_percent = 100.0;

        long long p1TotalNodesAccum = 0;
        long long p2TotalNodesAccum = 0;

        while (!board.isGameOver() && turns < 200) {

            bool p1CanMove = Chiung::AtaxxEngine::hasValidMoves(board, Chiung::PLAYER1);
            bool p2CanMove = Chiung::AtaxxEngine::hasValidMoves(board, Chiung::PLAYER2);

            if (!p1CanMove || !p2CanMove) {
                uint64_t empty = ~(board.p1 | board.p2) & Chiung::FULL_BOARD_MASK;
                if (p1CanMove && !p2CanMove) {
                    board.p1 |= empty;
                    if (SHOW_BROADCAST) cout << "🧹 [싹쓸이 발동] 팀원 AI 고립! 치웅 AI(P1)가 남은 빈칸을 모두 복제하며 즉시 승리합니다!\n";
                }
                else if (!p1CanMove && p2CanMove) {
                    board.p2 |= empty;
                    if (SHOW_BROADCAST) cout << "🧹 [싹쓸이 발동] 치웅 AI 고립! 팀원 AI(P2)가 남은 빈칸을 모두 복제하며 즉시 승리합니다!\n";
                }
                break;
            }

            int p1Count = Chiung::popcount64(board.p1);
            int p2Count = Chiung::popcount64(board.p2);
            int totalCount = p1Count + p2Count;

            if (totalCount > 4) {
                double currentP1Percent = ((double)p1Count / totalCount) * 100.0;
                double currentP2Percent = ((double)p2Count / totalCount) * 100.0;
                if (currentP1Percent < lowest_p1_percent) lowest_p1_percent = currentP1Percent;
                if (currentP2Percent < lowest_p2_percent) lowest_p2_percent = currentP2Percent;
            }

            int currColor = (turns % 2 == 0 ? Chiung::PLAYER1 : Chiung::PLAYER2);

            if (currColor == Chiung::PLAYER1) {
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
                    }
                    board = Chiung::AtaxxEngine::applyMove(board, bestMove, Chiung::PLAYER1);
                }
            }
            else {
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
    cout << "✅ 시뮬레이션 끝! 모든 지표가 '" << outFileName << "' 파일에 저장되었습니다.\n";
    return 0;
}