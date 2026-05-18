#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <iomanip>

// =====================================================================
// 💡 [유니버설 플레이어 설정]
// 팀원과 코드를 공유할 때, 아래의 숫자만 변경하여 대진표를 만드세요!
// =====================================================================
#define ENGINE_CHIUNG 1
#define ENGINE_TAEWAN 2
#define ENGINE_BOSS   3

// 👇 여기서 P1(흑돌)과 P2(백돌)에 참가할 엔진을 숫자로 지정하세요.
#define MATCH_P1 ENGINE_CHIUNG
#define MATCH_P2 ENGINE_TAEWAN

const bool SHOW_BROADCAST = false; // true: 관전 모드, false: 데이터 추출 모드
// =====================================================================

// [핵심 방어 1] 우리 엔진을 먼저 불러와서 팀원 매크로 오염으로부터 보호합니다.
#include "ChiungEngine.h" 

#if (MATCH_P1 == ENGINE_BOSS) || (MATCH_P2 == ENGINE_BOSS)
#include "BossEngine.h"
#endif

// 팀원 엔진은 반드시 가장 마지막에 불러와야 다른 엔진들을 망가뜨리지 않습니다.
#if (MATCH_P1 == ENGINE_TAEWAN) || (MATCH_P2 == ENGINE_TAEWAN)
#include "TeammateEngine.h"
#endif

using namespace std;
using namespace std::chrono;

// ---------------------------------------------------------------------
// 엔진 이름 자동 매핑 함수
// ---------------------------------------------------------------------
string getEngineName(int engineType) {
    if (engineType == ENGINE_CHIUNG) return "치웅_V11";
    if (engineType == ENGINE_TAEWAN) return "태완_V2";
    if (engineType == ENGINE_BOSS)   return "보스_카운터";
    return "Unknown";
}

// ---------------------------------------------------------------------
// 보드판 변환 어댑터
// ---------------------------------------------------------------------
#if (MATCH_P1 == ENGINE_TAEWAN) || (MATCH_P2 == ENGINE_TAEWAN)
Teammate::Bitboard convertToTeammateBoard(const Chiung::Bitboard& cb) {
    Teammate::Bitboard tb;
    tb.p1 = cb.p1; tb.p2 = cb.p2; tb.hashKey = 0;
    uint64_t temp1 = tb.p1;
    while (temp1) { int idx = Teammate::ctz64(temp1); tb.hashKey ^= Teammate::ZOBRIST_TABLE[idx][0]; temp1 &= temp1 - 1; }
    uint64_t temp2 = tb.p2;
    while (temp2) { int idx = Teammate::ctz64(temp2); tb.hashKey ^= Teammate::ZOBRIST_TABLE[idx][1]; temp2 &= temp2 - 1; }
    tb.hashKey ^= Teammate::ZOBRIST_TURN;
    return tb;
}
#endif

#if (MATCH_P1 == ENGINE_BOSS) || (MATCH_P2 == ENGINE_BOSS)
Boss::Bitboard convertToBossBoard(const Chiung::Bitboard& cb) {
    Boss::Bitboard bb;
    bb.p1 = cb.p1; bb.p2 = cb.p2; bb.hashKey = 0;
    uint64_t temp1 = bb.p1;
    while (temp1) { int idx = Boss::ctz64(temp1); bb.hashKey ^= Boss::ZOBRIST_TABLE[idx][0]; temp1 &= temp1 - 1; }
    uint64_t temp2 = bb.p2;
    while (temp2) { int idx = Boss::ctz64(temp2); bb.hashKey ^= Boss::ZOBRIST_TABLE[idx][1]; temp2 &= temp2 - 1; }
    bb.hashKey ^= Boss::ZOBRIST_TURN;
    return bb;
}
#endif

// 공통 고립 검사 함수 (매크로 충돌 방지를 위해 상수 대신 숫자 1 사용)
bool checkValidMovesGlobally(const Chiung::Bitboard& b, int color) {
    uint64_t my = (color == 1) ? b.p1 : b.p2;
    uint64_t empty = ~(b.p1 | b.p2) & ((1ULL << 49) - 1);
    uint64_t temp = my;
    while (temp) {
        int idx = Chiung::ctz64(temp);
        if ((Chiung::ADJ_MASK[idx] | Chiung::JUMP_MASK[idx]) & empty) return true;
        temp &= temp - 1;
    }
    return false;
}

// ---------------------------------------------------------------------
// 엔진 독립적 통신 구조체 및 래퍼 함수 (숫자 1, 2 직접 사용)
// ---------------------------------------------------------------------
struct MoveInfo {
    int from, to;
    bool isClone;
    double timeMs;
};

MoveInfo getP1Move(const Chiung::Bitboard& b, double timeLimit, long long& outNodes) {
#if MATCH_P1 == ENGINE_CHIUNG
    auto start = high_resolution_clock::now();
    Chiung::Move m = Chiung::AtaxxEngine::getBestMoveTimeLimited(b, 1, timeLimit);
    auto end = high_resolution_clock::now();
    outNodes = Chiung::AtaxxEngine::totalGameNodes.load();
    return { m.from, m.to, m.isClone, duration<double, std::milli>(end - start).count() };
#elif MATCH_P1 == ENGINE_TAEWAN
    Teammate::Bitboard tb = convertToTeammateBoard(b);
    auto start = high_resolution_clock::now();
    Teammate::Move m = Teammate::AtaxxEngine::getBestMoveTimeLimited(tb, 1, timeLimit, 1, 2, 3);
    auto end = high_resolution_clock::now();
    outNodes = Teammate::AtaxxEngine::totalNodes.load();
    return { m.from, m.to, m.isClone, duration<double, std::milli>(end - start).count() };
#elif MATCH_P1 == ENGINE_BOSS
    Boss::Bitboard bb = convertToBossBoard(b);
    auto start = high_resolution_clock::now();
    Boss::Move m = Boss::AtaxxEngine::getBestMoveTimeLimited(bb, 1, timeLimit);
    auto end = high_resolution_clock::now();
    outNodes = Boss::AtaxxEngine::totalNodes.load();
    return { m.from, m.to, m.isClone, duration<double, std::milli>(end - start).count() };
#endif
}

MoveInfo getP2Move(const Chiung::Bitboard& b, double timeLimit, long long& outNodes) {
#if MATCH_P2 == ENGINE_CHIUNG
    auto start = high_resolution_clock::now();
    Chiung::Move m = Chiung::AtaxxEngine::getBestMoveTimeLimited(b, 2, timeLimit);
    auto end = high_resolution_clock::now();
    outNodes = Chiung::AtaxxEngine::totalGameNodes.load();
    return { m.from, m.to, m.isClone, duration<double, std::milli>(end - start).count() };
#elif MATCH_P2 == ENGINE_TAEWAN
    Teammate::Bitboard tb = convertToTeammateBoard(b);
    auto start = high_resolution_clock::now();
    Teammate::Move m = Teammate::AtaxxEngine::getBestMoveTimeLimited(tb, 2, timeLimit, 1, 2, 3);
    auto end = high_resolution_clock::now();
    outNodes = Teammate::AtaxxEngine::totalNodes.load();
    return { m.from, m.to, m.isClone, duration<double, std::milli>(end - start).count() };
#elif MATCH_P2 == ENGINE_BOSS
    Boss::Bitboard bb = convertToBossBoard(b);
    auto start = high_resolution_clock::now();
    Boss::Move m = Boss::AtaxxEngine::getBestMoveTimeLimited(bb, 2, timeLimit);
    auto end = high_resolution_clock::now();
    outNodes = Boss::AtaxxEngine::totalNodes.load();
    return { m.from, m.to, m.isClone, duration<double, std::milli>(end - start).count() };
#endif
}

// ---------------------------------------------------------------------
// 편의 기능 함수들
// ---------------------------------------------------------------------
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
    cout << "📊 현재 스코어 -> [P1 " << getEngineName(MATCH_P1) << "(●): " << Chiung::popcount64(b.p1)
        << "개] vs [P2 " << getEngineName(MATCH_P2) << "(○): " << Chiung::popcount64(b.p2) << "개]\n";
    cout << "---------------------------------------------------------\n\n";
}

string getNextFilename(const string& baseName, const string& extension) {
    int counter = 1;
    string filename;
    while (true) {
        filename = baseName + "_" + to_string(counter) + extension;
        ifstream f(filename.c_str());
        if (!f.good()) { break; }
        counter++;
    }
    return filename;
}

int main() {
    Chiung::AtaxxEngine::initMasks();
#if (MATCH_P1 == ENGINE_BOSS) || (MATCH_P2 == ENGINE_BOSS)
    Boss::AtaxxEngine::initMasks();
#endif
#if (MATCH_P1 == ENGINE_TAEWAN) || (MATCH_P2 == ENGINE_TAEWAN)
    Teammate::initEngine();
#endif

    string p1Name = getEngineName(MATCH_P1);
    string p2Name = getEngineName(MATCH_P2);

    string outFileName = getNextFilename("Metrics_" + p1Name + "_VS_" + p2Name, ".csv");
    ofstream csv(outFileName);

    csv << "게임번호,시간제한(초),승리자,"
        << p1Name << "_최저점유율_%," << p2Name << "_최저점유율_%,"
        << p1Name << "_평균시간_ms," << p2Name << "_평균시간_ms,"
        << p1Name << "_NPS," << p2Name << "_NPS,총턴수\n";

    cout << "=========================================================\n";
    cout << " 🚀 유니버설 AI 데스매치 시뮬레이터\n";
    cout << "=========================================================\n";
    cout << "- P1 (흑●): " << p1Name << "\n";
    cout << "- P2 (백○): " << p2Name << "\n";
    cout << "💾 저장될 파일명: " << outFileName << "\n\n";

    int gameID = 1;
    const int ITERATIONS = 100;
    const double TIME_LIMIT = 0.05;

    cout << "[제한시간 " << TIME_LIMIT << "초] " << ITERATIONS << "판 시뮬레이션 진행 중...\n";

    int p1Wins = 0, p2Wins = 0, draws = 0;

    for (int iter = 1; iter <= ITERATIONS; iter++) {
        fill(Chiung::TT_Cache.begin(), Chiung::TT_Cache.end(), Chiung::TTEntry{ 0, -1, 0, Chiung::EXACT });
#if (MATCH_P1 == ENGINE_BOSS) || (MATCH_P2 == ENGINE_BOSS)
        fill(Boss::TT_Cache.begin(), Boss::TT_Cache.end(), Boss::TTEntry{ 0, -1, 0, Boss::EXACT, Boss::Move() });
#endif
#if (MATCH_P1 == ENGINE_TAEWAN) || (MATCH_P2 == ENGINE_TAEWAN)
        fill(Teammate::TT_Cache.begin(), Teammate::TT_Cache.end(), Teammate::TTEntry{ 0, -1, Teammate::EXACT, 0 });
#endif

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

            bool p1CanMove = checkValidMovesGlobally(board, 1);
            bool p2CanMove = checkValidMovesGlobally(board, 2);

            if (!p1CanMove || !p2CanMove) {
                uint64_t empty = ~(board.p1 | board.p2) & ((1ULL << 49) - 1);
                if (p1CanMove && !p2CanMove) {
                    board.p1 |= empty;
                    if (SHOW_BROADCAST) cout << "🧹 [싹쓸이 발동] P2 " << p2Name << " 고립! P1 " << p1Name << "가 빈칸을 복제하며 즉시 승리!\n";
                }
                else if (!p1CanMove && p2CanMove) {
                    board.p2 |= empty;
                    if (SHOW_BROADCAST) cout << "🧹 [싹쓸이 발동] P1 " << p1Name << " 고립! P2 " << p2Name << "가 빈칸을 복제하며 즉시 승리!\n";
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

            int currColor = (turns % 2 == 0 ? 1 : 2);

            if (currColor == 1) {
                long long turnNodes = 0;
                MoveInfo moveInfo = getP1Move(board, TIME_LIMIT, turnNodes);

                p1TotalTimeMs += moveInfo.timeMs;
                p1TotalNodesAccum += turnNodes;
                p1MovesCount++;

                if (moveInfo.to != -1) {
                    if (SHOW_BROADCAST) {
                        int r2 = moveInfo.to / 7, c2 = moveInfo.to % 7;
                        if (moveInfo.isClone) cout << "➡️ ⚡ P1 " << p1Name << ": (" << r2 << ", " << c2 << ") 로 1칸 [복제] 완료!\n";
                        else cout << "➡️ 🔥 P1 " << p1Name << ": (" << moveInfo.from / 7 << ", " << moveInfo.from % 7 << ") 에서 (" << r2 << ", " << c2 << ") 로 2칸 [점프] 완료!\n";
                    }
                    Chiung::Move cMove = { moveInfo.from, moveInfo.to, moveInfo.isClone, 0, 0 };
                    board = Chiung::AtaxxEngine::applyMove(board, cMove, 1);
                }
            }
            else {
                long long turnNodes = 0;
                MoveInfo moveInfo = getP2Move(board, TIME_LIMIT, turnNodes);

                p2TotalTimeMs += moveInfo.timeMs;
                p2TotalNodesAccum += turnNodes;
                p2MovesCount++;

                if (moveInfo.to != -1) {
                    if (SHOW_BROADCAST) {
                        int r2 = moveInfo.to / 7, c2 = moveInfo.to % 7;
                        if (moveInfo.isClone) cout << "➡️ ⚡ P2 " << p2Name << ": (" << r2 << ", " << c2 << ") 로 1칸 [복제] 완료!\n";
                        else cout << "➡️ 🔥 P2 " << p2Name << ": (" << moveInfo.from / 7 << ", " << moveInfo.from % 7 << ") 에서 (" << r2 << ", " << c2 << ") 로 2칸 [점프] 완료!\n";
                    }
                    Chiung::Move cMove = { moveInfo.from, moveInfo.to, moveInfo.isClone, 0, 0 };
                    board = Chiung::AtaxxEngine::applyMove(board, cMove, 2);
                }
            }

            if (SHOW_BROADCAST) printBoard(board);
            turns++;
        }

        int winner = board.getWinner();
        if (winner == 1) p1Wins++;
        else if (winner == 2) p2Wins++;
        else draws++;

        double p1AvgTime = (p1MovesCount > 0) ? (p1TotalTimeMs / p1MovesCount) : 0.0;
        double p2AvgTime = (p2MovesCount > 0) ? (p2TotalTimeMs / p2MovesCount) : 0.0;

        long long p1Nps = (p1TotalTimeMs > 0) ? (long long)(p1TotalNodesAccum / (p1TotalTimeMs / 1000.0)) : 0;
        long long p2Nps = (p2TotalTimeMs > 0) ? (long long)(p2TotalNodesAccum / (p2TotalTimeMs / 1000.0)) : 0;

        csv << gameID++ << "," << TIME_LIMIT << "," << winner << "," << lowest_p1_percent << "," << lowest_p2_percent << ","
            << p1AvgTime << "," << p2AvgTime << "," << p1Nps << "," << p2Nps << "," << turns << "\n";

        if (!SHOW_BROADCAST && iter % 10 == 0) {
            cout << "  -> " << iter << "판 완료... [스코어] P1(" << p1Name << "): " << p1Wins << "승 | P2(" << p2Name << "): " << p2Wins << "승 | 무승부: " << draws << "\n";
        }
        else if (SHOW_BROADCAST) {
            cout << "🏁 " << iter << "번째 판 종료! 승자: " << (winner == 1 ? "P1 (" + p1Name + ")" : (winner == 2 ? "P2 (" + p2Name + ")" : "무승부")) << "\n";
        }
    }

    cout << "\n🔥 대결 최종 결과 | P1 승: " << p1Wins << " / P2 승: " << p2Wins << " / 무승부: " << draws << "\n\n";

    csv.close();
    cout << "✅ 모든 지표가 '" << outFileName << "' 파일에 저장되었습니다.\n";
    return 0;
}