#include <iostream>
#include <string>
#include "ChiungEngine.h" // 폰더링 탑재 최신 엔진

using namespace std;

int main() {
    // 파이썬 GUI와의 원활한 실시간 통신을 위해 입출력 버퍼링 완전 해제
    setvbuf(stdout, NULL, _IONBF, 0);

    Chiung::AtaxxEngine::initMasks();
    Chiung::Bitboard board;
    board.init();

    int myColor = 1;
    string cmd;

    // 파이썬 프로그램이 명령을 내릴 때까지 무한 대기
    while (cin >> cmd) {
        if (cmd == "INIT") {
            cin >> myColor;
            // [추가] 재시작 버튼을 누르면 엔진의 보드판도 초기 상태로 리셋!
            board.init();
            cout << "READY" << endl;
        }
        else if (cmd == "OPP") {
            int fr, fc, tr, tc;
            cin >> fr >> fc >> tr >> tc;

            // [추가] 상대방이 둘 곳이 없어서 패스(Pass)한 경우
            if (fr == -1 && fc == -1) {
                board.hashKey ^= Chiung::ZOBRIST_TURN; // 턴만 넘김
            }
            else {
                int from = fr * 7 + fc;
                int to = tr * 7 + tc;
                int dist = max(abs(fr - tr), abs(fc - tc));
                Chiung::Move m = { from, to, (dist == 1), 0, 0 };
                board = Chiung::AtaxxEngine::applyMove(board, m, (myColor == 1 ? 2 : 1));
            }
            cout << "ACK" << endl;
        }
        else if (cmd == "THINK") {
            int emptyCount = 49 - (Chiung::popcount64(board.p1) + Chiung::popcount64(board.p2));
            double currentLimit = 3.0; // 기본 3초

            if (emptyCount <= 35 && emptyCount > 15) currentLimit = 5.0;
            else if (emptyCount <= 15) currentLimit = 7.0;

            Chiung::Move best = Chiung::AtaxxEngine::getBestMoveTimeLimited(board, myColor, currentLimit);

            // [추가] AI 본인도 둘 곳이 없어서 패스(Pass)해야 하는 경우
            if (best.from == -1) {
                board.hashKey ^= Chiung::ZOBRIST_TURN;
                cout << "BESTMOVE -1 -1 -1 -1 0" << endl;
            }
            else {
                board = Chiung::AtaxxEngine::applyMove(board, best, myColor);
                cout << "BESTMOVE " << (best.from / 7) << " " << (best.from % 7) << " "
                    << (best.to / 7) << " " << (best.to % 7) << " " << (best.isClone ? 1 : 0) << endl;
            }
        }
    }
    return 0;
}
