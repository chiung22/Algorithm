#include <iostream>
#include <string>
#include "ChiungEngine2.h" // [수정] 버전2 헤더파일 포함

using namespace std;

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    
    Chiung::AtaxxEngine::initMasks();
    Chiung::Bitboard board;
    board.init();

    int myColor = 1;
    string cmd;

    while (cin >> cmd) {
        if (cmd == "INIT") {
            cin >> myColor;
            board.init(); 
            cout << "READY" << endl;
        }
        else if (cmd == "OPP") {
            int fr, fc, tr, tc;
            cin >> fr >> fc >> tr >> tc;
            
            if (fr == -1 && fc == -1) {
                board.hashKey ^= Chiung::ZOBRIST_TURN;
            } 
            else {
                int from = fr * 7 + fc;
                int to = tr * 7 + tc;
                int dist = max(abs(fr - tr), abs(fc - tc));
                Chiung::Move m = {from, to, (dist == 1), 0, 0};
                board = Chiung::AtaxxEngine::applyMove(board, m, (myColor == 1 ? 2 : 1));
            }
            cout << "ACK" << endl;
        }
        else if (cmd == "THINK") {
            int emptyCount = 49 - (Chiung::popcount64(board.p1) + Chiung::popcount64(board.p2));
            double currentLimit = 3.0; 
            
            if (emptyCount <= 35 && emptyCount > 15) currentLimit = 5.0;
            else if (emptyCount <= 15) currentLimit = 7.0;

            Chiung::Move best = Chiung::AtaxxEngine::getBestMoveTimeLimited(board, myColor, currentLimit);
            
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
