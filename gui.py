import tkinter as tk
import subprocess
import threading
import sys

class AtaxxGUI(tk.Tk):
    def __init__(self, engine_path, my_color):
        super().__init__()
        self.title("Chiung Alpha Protocol - Tournament GUI")
        self.geometry("600x800") # 버튼 공간을 위해 창 높이 확장
        self.configure(bg="#1E1E1E")
        self.resizable(False, False)
        
        self.engine_path = engine_path
        self.my_color = my_color
        self.opp_color = 2 if my_color == 1 else 1
        
        self.board = [[0]*7 for _ in range(7)]
        self.board[0][0] = self.board[6][6] = 1 # Black
        self.board[0][6] = self.board[6][0] = 2 # White
        
        self.current_turn = 1
        self.selected_pos = None
        self.game_over = False
        
        try:
            self.engine = subprocess.Popen([self.engine_path], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
            self.send_cmd(f"INIT {self.my_color}")
            self.engine.stdout.readline() 
        except FileNotFoundError:
            print("❌ 오류: 'algorithm_final.exe' 파일을 찾을 수 없습니다. 파이썬 파일과 같은 폴더에 넣어주세요.")
            sys.exit()
            
        self.setup_ui()
        self.draw_board()
        
        if self.current_turn == self.my_color:
            self.start_ai_turn()

    def send_cmd(self, cmd):
        self.engine.stdin.write(cmd + "\n")
        self.engine.stdin.flush()

    def setup_ui(self):
        self.info_label = tk.Label(self, text="게임 준비 완료", font=("맑은 고딕", 16, "bold"), bg="#1E1E1E", fg="#FFFFFF")
        self.info_label.pack(pady=15)
        
        self.canvas = tk.Canvas(self, width=560, height=560, bg="#2C3E50", highlightthickness=0)
        self.canvas.pack()
        self.canvas.bind("<Button-1>", self.on_click)

        # [추가] 재시작 버튼 UI
        btn_frame = tk.Frame(self, bg="#1E1E1E")
        btn_frame.pack(pady=15)
        self.restart_btn = tk.Button(btn_frame, text="🔄 게임 재시작", font=("맑은 고딕", 14, "bold"), bg="#E67E22", fg="#FFFFFF", width=20, cursor="hand2", command=self.restart_game)
        self.restart_btn.pack()

    def restart_game(self):
        # [추가] 재시작 로직 (보드 리셋 및 엔진 초기화)
        self.board = [[0]*7 for _ in range(7)]
        self.board[0][0] = self.board[6][6] = 1
        self.board[0][6] = self.board[6][0] = 2
        
        self.current_turn = 1
        self.selected_pos = None
        self.game_over = False
        
        self.send_cmd(f"INIT {self.my_color}")
        self.engine.stdout.readline() # ACK 대기
        
        self.draw_board()
        if self.current_turn == self.my_color:
            self.start_ai_turn()

    # [추가] 플레이어가 이동 가능한 칸이 남아있는지 스캔하는 함수
    def has_valid_moves(self, color):
        for r in range(7):
            for c in range(7):
                if self.board[r][c] == color:
                    for dr in [-2, -1, 0, 1, 2]:
                        for dc in [-2, -1, 0, 1, 2]:
                            if dr == 0 and dc == 0: continue
                            nr, nc = r + dr, c + dc
                            if 0 <= nr < 7 and 0 <= nc < 7 and self.board[nr][nc] == 0:
                                return True
        return False

    def draw_board(self):
        self.canvas.delete("all")
        cell_size = 80
        for r in range(7):
            for c in range(7):
                x0, y0 = c * cell_size, r * cell_size
                x1, y1 = x0 + cell_size, y0 + cell_size
                
                color = "#34495E"
                if self.selected_pos == (r, c): color = "#F1C40F"
                elif self.selected_pos:
                    sr, sc = self.selected_pos
                    dist = max(abs(sr-r), abs(sc-c))
                    if dist == 1 and self.board[r][c] == 0: color = "#27AE60"
                    elif dist == 2 and self.board[r][c] == 0: color = "#2980B9"
                        
                self.canvas.create_rectangle(x0, y0, x1, y1, fill=color, outline="#1E1E1E", width=2)
                
                val = self.board[r][c]
                if val != 0:
                    pc = "#000000" if val == 1 else "#FFFFFF"
                    self.canvas.create_oval(x0+10, y0+10, x1-10, y1-10, fill=pc, outline="#7F8C8D", width=2)
        
        b_cnt = sum(row.count(1) for row in self.board)
        w_cnt = sum(row.count(2) for row in self.board)
        turn_text = "🤖 치웅 AI 생각 중..." if self.current_turn == self.my_color else "👤 상대방 입력 대기 중..."
        self.info_label.config(text=f"흑(B): {b_cnt}  |  백(W): {w_cnt}\n{turn_text}")
        
        b_can_move = self.has_valid_moves(1)
        w_can_move = self.has_valid_moves(2)

        # [추가] 양쪽 모두 갇혀서 움직일 곳이 없으면 즉시 게임 종료 (고착 상태 판별)
        if b_cnt == 0 or w_cnt == 0 or b_cnt + w_cnt == 49 or (not b_can_move and not w_can_move):
            self.game_over = True
            winner = "흑(Black)" if b_cnt > w_cnt else ("백(White)" if w_cnt > b_cnt else "무승부")
            self.info_label.config(text=f"🏁 게임 종료! 승리: {winner}\n(흑: {b_cnt} vs 백: {w_cnt})", fg="#F1C40F")
            return

    def apply_move_logic(self, fr, fc, tr, tc, color):
        dist = max(abs(fr-tr), abs(fc-tc))
        if dist > 1: self.board[fr][fc] = 0
        self.board[tr][tc] = color
        
        for dr in [-1, 0, 1]:
            for dc in [-1, 0, 1]:
                if dr == 0 and dc == 0: continue
                nr, nc = tr + dr, tc + dc
                if 0 <= nr < 7 and 0 <= nc < 7 and self.board[nr][nc] != 0:
                    self.board[nr][nc] = color

    def on_click(self, event):
        if self.current_turn == self.my_color or self.game_over: return
            
        c, r = event.x // 80, event.y // 80
        
        if self.selected_pos:
            sr, sc = self.selected_pos
            if sr == r and sc == c: self.selected_pos = None
            else:
                dist = max(abs(sr-r), abs(sc-c))
                if dist <= 2 and self.board[r][c] == 0:
                    self.apply_move_logic(sr, sc, r, c, self.opp_color)
                    self.selected_pos = None
                    self.draw_board()
                    
                    self.send_cmd(f"OPP {sr} {sc} {r} {c}")
                    self.engine.stdout.readline() # ACK
                    
                    self.current_turn = self.my_color
                    self.start_ai_turn()
                elif self.board[r][c] == self.opp_color:
                    self.selected_pos = (r, c)
        else:
            if self.board[r][c] == self.opp_color: self.selected_pos = (r, c)
        self.draw_board()

    def start_ai_turn(self):
        if self.game_over: return
        self.info_label.config(fg="#E74C3C")
        self.update()
        threading.Thread(target=self.wait_for_ai, daemon=True).start()

    def wait_for_ai(self):
        self.send_cmd("THINK")
        resp = self.engine.stdout.readline().strip()
        if resp.startswith("BESTMOVE"):
            _, fr, fc, tr, tc, isClone = resp.split()
            fr, fc, tr, tc = map(int, [fr, fc, tr, tc])
            
            # [추가] AI가 둘 곳이 없어 패스한 경우 (-1 리턴)
            if fr != -1:
                self.apply_move_logic(fr, fc, tr, tc, self.my_color)
                
            self.current_turn = self.opp_color
            self.after(0, self.finish_ai_turn, fr, fc, tr, tc)

    def finish_ai_turn(self, fr, fc, tr, tc):
        self.draw_board()
        if self.game_over: return
        
        self.info_label.config(fg="#2ECC71")
        
        # [추가] 턴이 상대방(인간)에게 넘어갔는데 인간이 둘 곳이 없는 경우 자동 패스
        if self.current_turn == self.opp_color and not self.has_valid_moves(self.opp_color):
            self.info_label.config(text=f"{self.info_label.cget('text')}\n(상대방 이동 불가: 자동 턴 패스)")
            self.update()
            self.after(1500, self.force_human_pass)

    def force_human_pass(self):
        self.send_cmd("OPP -1 -1 -1 -1")
        self.engine.stdout.readline() # ACK
        self.current_turn = self.my_color
        self.start_ai_turn()

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