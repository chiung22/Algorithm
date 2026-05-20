#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <iomanip>

// =====================================================================
// 💡 [유니버설 플레이어 설정]
// =====================================================================
#define ENGINE_CHIUNG 1
#define ENGINE_TAEWAN 2
#define ENGINE_BOSS   3

// 👇 처음에 시작할 P1(흑)과 P2(백)를 지정하세요. (절반이 지나면 자동으로 바뀝니다)
#define MATCH_P1 ENGINE_CHIUNG
#define MATCH_P2 ENGINE_BOSS

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
    if (engineType == ENGINE_CHIUNG) return "치웅_V16";
    if (engineType == ENGINE_TAEWAN) return "태완_V3";
    if (engineType == ENGINE_BOSS)   return "보스_카운터_V2";
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

// 공통 고립 검사 함수
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
// [수정 포인트 1] 동적 엔진 호출 래퍼 (런타임 진영 교체를 위함)
// ---------------------------------------------------------------------
struct MoveInfo {
    int from, to;
    bool isClone;
    double timeMs;
};

MoveInfo getEngineMove(int engineType, const Chiung::Bitboard& b, int color, double timeLimit, long long& outNodes) {
    if (engineType == ENGINE_CHIUNG) {
        auto start = high_resolution_clock::now();
        Chiung::Move m = Chiung::AtaxxEngine::getBestMoveTimeLimited(b, color, timeLimit);
        auto end = high_resolution_clock::now();
        outNodes = Chiung::AtaxxEngine::totalGameNodes.load();
        return { m.from, m.to, m.isClone, duration<double, std::milli>(end - start).count() };
    }
#if (MATCH_P1 == ENGINE_BOSS) || (MATCH_P2 == ENGINE_BOSS)
    else if (engineType == ENGINE_BOSS) {
        Boss::Bitboard bb = convertToBossBoard(b);
        auto start = high_resolution_clock::now();
        Boss::Move m = Boss::AtaxxEngine::getBestMoveTimeLimited(bb, color, timeLimit);
        auto end = high_resolution_clock::now();
        outNodes = Boss::AtaxxEngine::totalNodes.load();
        return { m.from, m.to, m.isClone, duration<double, std::milli>(end - start).count() };
    }
#endif
#if (MATCH_P1 == ENGINE_TAEWAN) || (MATCH_P2 == ENGINE_TAEWAN)
    else if (engineType == ENGINE_TAEWAN) {
        Teammate::Bitboard tb = convertToTeammateBoard(b);
        auto start = high_resolution_clock::now();
        Teammate::Move m = Teammate::AtaxxEngine::getBestMoveTimeLimited(tb, color, timeLimit, 1, 2, 3);
        auto end = high_resolution_clock::now();
        outNodes = Teammate::AtaxxEngine::totalNodes.load();
        return { m.from, m.to, m.isClone, duration<double, std::milli>(end - start).count() };
    }
#endif
    return { -1, -1, false, 0.0 };
}

// ---------------------------------------------------------------------
// 편의 기능 함수들
// ---------------------------------------------------------------------
void printBoard(const Chiung::Bitboard& b, int current_p1, int current_p2) {
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
    cout << "📊 현재 스코어 -> [P1 " << getEngineName(current_p1) << "(●): " << Chiung::popcount64(b.p1)
        << "개] vs [P2 " << getEngineName(current_p2) << "(○): " << Chiung::popcount64(b.p2) << "개]\n";
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

    string outFileName = getNextFilename("Metrics_AutoSwap_Analysis", ".csv");
    ofstream csv(outFileName);

    if (!csv.is_open()) {
        cout << "\n❌ [치명적 오류] CSV 파일을 생성할 수 없습니다!\n";
        cout << "해결 방법:\n";
        cout << "1. 엑셀에서 '" << outFileName << "' 파일이 이미 열려있다면 닫아주세요.\n";
        cout << "2. 백신 프로그램이 파일 생성을 차단하고 있는지 확인하세요.\n\n";
        system("pause");
        return 1;
    }

    // [수정 포인트 2] P1, P2 엔진 이름을 명시하는 컬럼 추가 (데이터 피벗 분석을 위함)
    csv << "게임번호,시간제한(초),P1_엔진(흑),P2_엔진(백),승리_진영,승리_엔진,"
        << "P1_최저점유율_%,P2_최저점유율_%,"
        << "P1_평균시간_ms,P2_평균시간_ms,"
        << "P1_NPS,P2_NPS,총턴수,"
        << "Opening_Advantage,Midgame_Volatility,Solver_Efficiency_Score,"
        << "P1_Clone_Count,P1_Jump_Count,P2_Clone_Count,P2_Jump_Count,"
        << "Corner_Grab_Turn,Chiung_Peak_Advantage,Turning_Point\n";

    int current_p1 = MATCH_P1;
    int current_p2 = MATCH_P2;

    cout << "=========================================================\n";
    cout << " 🚀 유니버설 AI 자동 진영교체(Swap) 시뮬레이터\n";
    cout << "=========================================================\n";
    cout << "💾 저장될 파일명: " << outFileName << "\n\n";

    int gameID = 1;
    const int ITERATIONS = 200;
    const double TIME_LIMIT = 0.05;

    cout << "[제한시간 " << TIME_LIMIT << "초] " << ITERATIONS << "판 시뮬레이션 진행 중...\n";

    // 누적 승수 트래커 (P1/P2 기준이 아닌 '치웅', '보스' 엔진 기준으로 집계)
    int engine1Wins = 0, engine2Wins = 0, draws = 0;

    for (int iter = 1; iter <= ITERATIONS; iter++) {
        // [수정 포인트 3] 판수의 절반이 넘어가면 흑/백 진영을 자동으로 맞바꿈
        if (iter == (ITERATIONS / 2) + 1) {
            swap(current_p1, current_p2);
            cout << "\n🔄 [진영 자동 교체] " << iter << "번째 판부터 흑/백 진영이 교체됩니다!\n";
            cout << "-> P1 (흑●): " << getEngineName(current_p1) << "\n";
            cout << "-> P2 (백○): " << getEngineName(current_p2) << "\n\n";
        }

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
            printBoard(board, current_p1, current_p2);
        }

        double p1TotalTimeMs = 0.0, p2TotalTimeMs = 0.0;
        int p1MovesCount = 0, p2MovesCount = 0;
        int turns = 0;

        double lowest_p1_percent = 100.0;
        double lowest_p2_percent = 100.0;

        long long p1TotalNodesAccum = 0;
        long long p2TotalNodesAccum = 0;

        int opening_advantage = 0;
        int score_at_34 = 0;
        int score_at_19 = 0;
        int midgame_volatility = 0;
        int solver_efficiency_score = 0;

        bool op_rec = false, m34_rec = false, m19_rec = false, sol_rec = false;

        int p1_clones = 0, p1_jumps = 0;
        int p2_clones = 0, p2_jumps = 0;
        int corner_grab_turn = -1;
        int peak_advantage = -999999;
        int turning_point_turn = -1;
        int prev_lead = 0;

        uint64_t corner_mask = (1ULL << 0) | (1ULL << 6) | (1ULL << 42) | (1ULL << 48);

        while (!board.isGameOver() && turns < 200) {

            bool p1CanMove = checkValidMovesGlobally(board, 1);
            bool p2CanMove = checkValidMovesGlobally(board, 2);

            if (!p1CanMove || !p2CanMove) {
                uint64_t empty = ~(board.p1 | board.p2) & ((1ULL << 49) - 1);
                if (p1CanMove && !p2CanMove) {
                    board.p1 |= empty;
                }
                else if (!p1CanMove && p2CanMove) {
                    board.p2 |= empty;
                }
                break;
            }

            int p1Count = Chiung::popcount64(board.p1);
            int p2Count = Chiung::popcount64(board.p2);
            int totalCount = p1Count + p2Count;
            int emptyCount = 49 - totalCount;

            if (emptyCount <= 35 && !op_rec) { opening_advantage = p1Count - p2Count; op_rec = true; }
            if (emptyCount <= 34 && !m34_rec) { score_at_34 = p1Count - p2Count; m34_rec = true; }
            if (emptyCount <= 19 && !m19_rec) { score_at_19 = p1Count - p2Count; m19_rec = true; }
            if (emptyCount <= 18 && !sol_rec) { solver_efficiency_score = p1Count - p2Count; sol_rec = true; }

            int current_lead = p1Count - p2Count;
            if (turns > 0 && turning_point_turn == -1) {
                if ((prev_lead > 0 && current_lead < 0) || (prev_lead < 0 && current_lead > 0)) {
                    turning_point_turn = turns;
                }
            }
            prev_lead = current_lead;

            // [수정 포인트 4] 현재 P1/P2 진영과 무관하게, 항상 '치웅 엔진(V16)' 시점에서의 Peak Advantage 추적
            int eval_color = (current_p1 == ENGINE_CHIUNG) ? 1 : (current_p2 == ENGINE_CHIUNG ? 2 : 0);
            if (eval_color != 0) {
                int current_eval = Chiung::AtaxxEngine::evaluate(board, eval_color);
                if (current_eval > peak_advantage && current_eval < 900000) {
                    peak_advantage = current_eval;
                }
            }

            if (totalCount > 4) {
                double currentP1Percent = ((double)p1Count / totalCount) * 100.0;
                double currentP2Percent = ((double)p2Count / totalCount) * 100.0;
                if (currentP1Percent < lowest_p1_percent) lowest_p1_percent = currentP1Percent;
                if (currentP2Percent < lowest_p2_percent) lowest_p2_percent = currentP2Percent;
            }

            int currColor = (turns % 2 == 0 ? 1 : 2);

            if (currColor == 1) {
                long long turnNodes = 0;
                MoveInfo moveInfo = getEngineMove(current_p1, board, 1, TIME_LIMIT, turnNodes);

                p1TotalTimeMs += moveInfo.timeMs;
                p1TotalNodesAccum += turnNodes;
                p1MovesCount++;

                if (moveInfo.to != -1) {
                    if (moveInfo.isClone) p1_clones++; else p1_jumps++;
                    Chiung::Move cMove = { moveInfo.from, moveInfo.to, moveInfo.isClone, 0, 0 };
                    board = Chiung::AtaxxEngine::applyMove(board, cMove, 1);
                }
            }
            else {
                long long turnNodes = 0;
                MoveInfo moveInfo = getEngineMove(current_p2, board, 2, TIME_LIMIT, turnNodes);

                p2TotalTimeMs += moveInfo.timeMs;
                p2TotalNodesAccum += turnNodes;
                p2MovesCount++;

                if (moveInfo.to != -1) {
                    if (moveInfo.isClone) p2_clones++; else p2_jumps++;
                    Chiung::Move cMove = { moveInfo.from, moveInfo.to, moveInfo.isClone, 0, 0 };
                    board = Chiung::AtaxxEngine::applyMove(board, cMove, 2);
                }
            }

            if (corner_grab_turn == -1 && ((board.p1 | board.p2) & corner_mask) != 0) {
                corner_grab_turn = turns;
            }

            if (SHOW_BROADCAST) printBoard(board, current_p1, current_p2);
            turns++;
        }

        int winner = board.getWinner();
        string winning_engine_name = "무승부";

        // [수정 포인트 5] 진영(P1/P2)이 아닌 엔진 기준으로 승률 누적 
        if (winner == 1) {
            winning_engine_name = getEngineName(current_p1);
            if (current_p1 == MATCH_P1) engine1Wins++; else engine2Wins++;
        }
        else if (winner == 2) {
            winning_engine_name = getEngineName(current_p2);
            if (current_p2 == MATCH_P1) engine1Wins++; else engine2Wins++;
        }
        else { draws++; }

        double p1AvgTime = (p1MovesCount > 0) ? (p1TotalTimeMs / p1MovesCount) : 0.0;
        double p2AvgTime = (p2MovesCount > 0) ? (p2TotalTimeMs / p2MovesCount) : 0.0;

        long long p1Nps = (p1TotalTimeMs > 0) ? (long long)(p1TotalNodesAccum / (p1TotalTimeMs / 1000.0)) : 0;
        long long p2Nps = (p2TotalTimeMs > 0) ? (long long)(p2TotalNodesAccum / (p2TotalTimeMs / 1000.0)) : 0;

        midgame_volatility = score_at_19 - score_at_34;

        csv << gameID++ << "," << TIME_LIMIT << ","
            << getEngineName(current_p1) << "," << getEngineName(current_p2) << ","
            << winner << "," << winning_engine_name << ","
            << lowest_p1_percent << "," << lowest_p2_percent << ","
            << p1AvgTime << "," << p2AvgTime << "," << p1Nps << "," << p2Nps << "," << turns << ","
            << opening_advantage << "," << midgame_volatility << "," << solver_efficiency_score << ","
            << p1_clones << "," << p1_jumps << "," << p2_clones << "," << p2_jumps << ","
            << corner_grab_turn << "," << peak_advantage << "," << turning_point_turn << endl;

        if (!SHOW_BROADCAST && iter % 10 == 0) {
            cout << "  -> " << iter << "판 완료... [누적 스코어] "
                << getEngineName(MATCH_P1) << ": " << engine1Wins << "승 | "
                << getEngineName(MATCH_P2) << ": " << engine2Wins << "승 | 무승부: " << draws << "\n";
        }
    }

    cout << "\n🔥 대결 최종 결과 | " << getEngineName(MATCH_P1) << " 승: " << engine1Wins
        << " / " << getEngineName(MATCH_P2) << " 승: " << engine2Wins << " / 무승부: " << draws << "\n\n";

    csv.close();
    cout << "✅ 모든 자동교체 분석 지표가 '" << outFileName << "' 파일에 성공적으로 저장되었습니다.\n";
    return 0;
}