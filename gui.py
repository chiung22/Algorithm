import tkinter as tk
import subprocess
import threading
import sys

class AtaxxGUI(tk.Tk):
    def __init__(self, engine_path, my_color):
        super().__init__()
        self.title("Chiung Alpha Protocol - Tournament GUI")
        self.geometry("600x750")
        self.configure(bg="#1E1E1E")
        self.resizable(False, False)
        
        self.engine_path = engine_path
        self.my_color = my_color
        self.opp_color = 2 if my_color == 1 else 1
        
        # 보드 초기화
        self.board = [[0]*7 for _ in range(7)]
        self.board[0][0] = self.board[6][6] = 1 # Black
        self.board[0][6] = self.board[6][0] = 2 # White
        
        self.current_turn = 1
        self.selected_pos = None
        self.game_over = False
        
        # C++ 엔진 백그라운드 실행
        try:
            self.engine = subprocess.Popen([self.engine_path], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
            # 시간 파라미터 제외, 색상만 전송
            self.send_cmd(f"INIT {self.my_color}")
            self.engine.stdout.readline() # 'READY' 수신 대기
        except FileNotFoundError:
            print("❌ 오류: 'algorithm_final.exe' 파일을 찾을 수 없습니다. 파이썬 파일과 같은 폴더에 넣어주세요.")
            sys.exit()
            
        self.setup_ui()
        self.draw_board()
        
        # 내가 흑돌(선공)이면 바로 AI 연산 시작
        if self.current_turn == self.my_color:
            self.start_ai_turn()

    def send_cmd(self, cmd):
        self.engine.stdin.write(cmd + "\n")
        self.engine.stdin.flush()

    def setup_ui(self):
        self.info_label = tk.Label(self, text="게임 준비 완료", font=("맑은 고딕", 16, "bold"), bg="#1E1E1E", fg="#FFFFFF")
        self.info_label.pack(pady=20)
        
        # 보드 캔버스
        self.canvas = tk.Canvas(self, width=560, height=560, bg="#2C3E50", highlightthickness=0)
        self.canvas.pack()
        self.canvas.bind("<Button-1>", self.on_click)

    def draw_board(self):
        self.canvas.delete("all")
        cell_size = 80
        for r in range(7):
            for c in range(7):
                x0, y0 = c * cell_size, r * cell_size
                x1, y1 = x0 + cell_size, y0 + cell_size
                
                # 칸 색상 결정 (하이라이트 효과)
                color = "#34495E" # 기본 타일 색상
                if self.selected_pos == (r, c):
                    color = "#F1C40F" # 선택한 돌 (노란색)
                elif self.selected_pos:
                    sr, sc = self.selected_pos
                    dist = max(abs(sr-r), abs(sc-c))
                    if dist == 1 and self.board[r][c] == 0:
                        color = "#27AE60" # 복제 가능 구역 (초록색)
                    elif dist == 2 and self.board[r][c] == 0:
                        color = "#2980B9" # 점프 가능 구역 (파란색)
                        
                self.canvas.create_rectangle(x0, y0, x1, y1, fill=color, outline="#1E1E1E", width=2)
                
                # 돌(Piece) 그리기
                val = self.board[r][c]
                if val != 0:
                    pc = "#000000" if val == 1 else "#FFFFFF"
                    self.canvas.create_oval(x0+10, y0+10, x1-10, y1-10, fill=pc, outline="#7F8C8D", width=2)
        
        # 점수판 업데이트
        b_cnt = sum(row.count(1) for row in self.board)
        w_cnt = sum(row.count(2) for row in self.board)
        turn_text = "🤖 치웅 AI 생각 중..." if self.current_turn == self.my_color else "👤 상대방 입력 대기 중..."
        self.info_label.config(text=f"흑(B): {b_cnt}  |  백(W): {w_cnt}\n{turn_text}")
        
        # 종료 체크
        if b_cnt == 0 or w_cnt == 0 or b_cnt + w_cnt == 49:
            self.game_over = True
            winner = "흑(Black)" if b_cnt > w_cnt else ("백(White)" if w_cnt > b_cnt else "무승부")
            self.info_label.config(text=f"🏁 게임 종료! 승리: {winner}", fg="#F1C40F")

    def apply_move_logic(self, fr, fc, tr, tc, color):
        dist = max(abs(fr-tr), abs(fc-tc))
        if dist > 1:
            self.board[fr][fc] = 0 # 점프 시 원래 자리 비움
        self.board[tr][tc] = color
        
        # 세균 감염(Infect) 로직
        for dr in [-1, 0, 1]:
            for dc in [-1, 0, 1]:
                if dr == 0 and dc == 0: continue
                nr, nc = tr + dr, tc + dc
                if 0 <= nr < 7 and 0 <= nc < 7 and self.board[nr][nc] != 0:
                    self.board[nr][nc] = color

    def on_click(self, event):
        if self.current_turn == self.my_color or self.game_over:
            return # AI 턴이거나 게임 종료 시 클릭 무시
            
        c = event.x // 80
        r = event.y // 80
        
        if self.selected_pos:
            sr, sc = self.selected_pos
            if sr == r and sc == c:
                self.selected_pos = None # 같은 돌 다시 누르면 취소
            else:
                dist = max(abs(sr-r), abs(sc-c))
                if dist <= 2 and self.board[r][c] == 0:
                    # 올바른 이동 처리
                    self.apply_move_logic(sr, sc, r, c, self.opp_color)
                    self.selected_pos = None
                    self.draw_board()
                    
                    # C++ 엔진에 상대방 수 전달
                    self.send_cmd(f"OPP {sr} {sc} {r} {c}")
                    self.engine.stdout.readline() # ACK 대기
                    
                    self.current_turn = self.my_color
                    self.start_ai_turn()
                elif self.board[r][c] == self.opp_color:
                    self.selected_pos = (r, c) # 다른 내 돌 선택
        else:
            if self.board[r][c] == self.opp_color:
                self.selected_pos = (r, c)
        
        self.draw_board()

    def start_ai_turn(self):
        if self.game_over: return
        self.info_label.config(fg="#E74C3C")
        self.update()
        
        # GUI가 멈추지 않도록 스레드에서 엔진 대기
        threading.Thread(target=self.wait_for_ai, daemon=True).start()

    def wait_for_ai(self):
        self.send_cmd("THINK")
        resp = self.engine.stdout.readline().strip()
        if resp.startswith("BESTMOVE"):
            _, fr, fc, tr, tc, isClone = resp.split()
            fr, fc, tr, tc = map(int, [fr, fc, tr, tc])
            
            # 파이썬 보드에 반영
            self.apply_move_logic(fr, fc, tr, tc, self.my_color)
            self.current_turn = self.opp_color
            
            # 메인 스레드에서 화면 갱신
            self.after(0, self.finish_ai_turn, fr, fc, tr, tc)

    def finish_ai_turn(self, fr, fc, tr, tc):
        self.draw_board()
        self.info_label.config(fg="#2ECC71")

if __name__ == "__main__":
    print("=========================================")
    print("  Chiung Alpha Protocol - GUI Launcher  ")
    print("=========================================\n")
    try:
        color = int(input("내 돌을 선택하세요 (1: 흑/선공, 2: 백/후공): "))
        app = AtaxxGUI("algorithm_final.exe", color)
        app.mainloop()
    except Exception as e:
        print("입력 오류 발생. 프로그램을 종료합니다.")