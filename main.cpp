#include <iostream>
#include <string>
#include "ChiungEngine.h" // 폰더링이 탑재된 최종 알파 엔진

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
            // 시간 입력 받던 부분을 제거하고 내 색상만 입력받음
            cin >> myColor;
            cout << "READY" << endl;
        }
        else if (cmd == "OPP") {
            int fr, fc, tr, tc;
            cin >> fr >> fc >> tr >> tc;
            int from = fr * 7 + fc;
            int to = tr * 7 + tc;
            int dist = max(abs(fr - tr), abs(fc - tc));
            Chiung::Move m = { from, to, (dist == 1), 0, 0 };
            board = Chiung::AtaxxEngine::applyMove(board, m, (myColor == 1 ? 2 : 1));
            cout << "ACK" << endl;
        }
        else if (cmd == "THINK") {
            // [자동 연산 시간 분배 로직] (초반: 3초, 중반: 5초, 후반 끝내기: 7초)
            int emptyCount = 49 - (Chiung::popcount64(board.p1) + Chiung::popcount64(board.p2));
            double currentLimit = 3.0; // 기본 초반 탐색 시간

            if (emptyCount <= 35 && emptyCount > 15) {
                currentLimit = 5.0; // 중반부 (복잡도 증가)
            }
            else if (emptyCount <= 15) {
                currentLimit = 7.0; // 후반부 (엔드게임 확정 계산)
            }

            // AI 최적 수 탐색
            Chiung::Move best = Chiung::AtaxxEngine::getBestMoveTimeLimited(board, myColor, currentLimit);
            board = Chiung::AtaxxEngine::applyMove(board, best, myColor);

            // 파이썬에게 결과 전송
            cout << "BESTMOVE " << (best.from / 7) << " " << (best.from % 7) << " "
                << (best.to / 7) << " " << (best.to % 7) << " " << (best.isClone ? 1 : 0) << endl;
        }
    }
    return 0;
}