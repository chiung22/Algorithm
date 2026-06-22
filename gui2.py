import tkinter as tk
from tkinter import messagebox
import subprocess
import threading
import sys
import os

class AtaxxGUI(tk.Tk):
    def __init__(self, engine_path):
        super().__init__()
        self.title("Chiung Alpha Protocol v2.0")
        self.geometry("600x850")
        self.resizable(False, False)
        self.configure(bg="#F5F6FA")
        
        self.engine_path = os.path.abspath(engine_path)
        self.engine = None
        self.game_over = False
        
        self.container = tk.Frame(self, bg="#F5F6FA")
        self.container.pack(fill="both", expand=True)
        self.container.grid_rowconfigure(0, weight=1)
        self.container.grid_columnconfigure(0, weight=1)
        
        self.selection_frame = tk.Frame(self.container, bg="#F5F6FA")
        self.selection_frame.grid(row=0, column=0, sticky="nsew")
        
        self.game_frame = tk.Frame(self.container, bg="#F5F6FA")
        self.game_frame.grid(row=0, column=0, sticky="nsew")
        
        self.build_selection_screen()
        self.build_game_screen()
        
        self.selection_frame.tkraise()

    def build_selection_screen(self):
        tk.Label(self.selection_frame, text="Chiung Alpha Protocol 2.0", font=("Arial", 24, "bold"), bg="#F5F6FA", fg="#2C3E50").pack(pady=(150, 20))
        tk.Label(self.selection_frame, text="플레이할 돌을 선택하세요", font=("Arial", 16), bg="#F5F6FA", fg="#2F3640").pack(pady=(0, 50))
        
        tk.Button(self.selection_frame, text="⚫ 흑돌 (내가 선공)", font=("Arial", 16, "bold"), width=20, height=2, command=lambda: self.start_game(1)).pack(pady=10)
        tk.Button(self.selection_frame, text="⚪ 백돌 (내가 후공)", font=("Arial", 16, "bold"), width=20, height=2, command=lambda: self.start_game(2)).pack(pady=10)

    def build_game_screen(self):
        self.info_label = tk.Label(self.game_frame, text="대기 중", font=("Arial", 16, "bold"), bg="#F5F6FA", fg="#2C3E50")
        self.info_label.pack(pady=10)
        
        self.canvas = tk.Canvas(self.game_frame, width=560, height=560, bg="#ECF0F1", highlightthickness=0)
        self.canvas.pack()
        self.canvas.bind("<Button-1>", self.on_click)

        btn_frame = tk.Frame(self.game_frame, bg="#F5F6FA")
        btn_frame.pack(pady=10)
        
        self.restart_btn = tk.Button(btn_frame, text="🔄 처음으로 (돌 다시 선택)", font=("Arial", 14, "bold"), width=25, cursor="hand2", command=self.restart_app)
        self.restart_btn.pack()

        self.log_label = tk.Label(self.game_frame, text=">> 시스템: 대기 중", font=("Consolas", 12, "bold"), bg="#F5F6FA", fg="#7F8C8D")
        self.log_label.pack(pady=5)

    def start_game(self, human_color):
        self.game_frame.tkraise()
        
        self.human_color = human_color
        self.engine_color = 2 if human_color == 1 else 1
        
        self.board = [[0]*7 for _ in range(7)]
        self.board[0][0] = self.board[6][6] = 1
        self.board[0][6] = self.board[6][0] = 2
        
        self.current_turn = 1
        self.selected_pos = None
        self.game_over = False
        
        self.draw_board()
        self.update() 
        
        threading.Thread(target=self.init_engine_thread, daemon=True).start()

    def update_log(self, msg, color="#34495E"):
        self.after(0, lambda: self.log_label.config(text=f">> 시스템: {msg}", fg=color))

    def init_engine_thread(self):
        try:
            self.update_log("엔진 연결 시도 중...", "#D35400")
            
            self.engine = subprocess.Popen([self.engine_path], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
            self.send_cmd(f"INIT {self.engine_color}")
            
            self.update_log("엔진 응답 대기 중...", "#D35400")
            
            resp = self.engine.stdout.readline()
            if not resp:
                self.after(0, self.on_engine_error, "엔진 실행 파일 누락 또는 실행 권한 없음")
                return
                
            self.after(0, self.on_engine_ready)
        except Exception as e:
            self.after(0, self.on_engine_error, str(e))

    def on_engine_ready(self):
        self.update_log("엔진 연결 완료! 게임을 시작합니다.", "#27AE60")
        if self.current_turn == self.engine_color:
            self.start_ai_turn()
        else:
            self.update_log("👤 플레이어(인간)의 턴입니다. 돌을 옮겨주세요.", "#27AE60")

    def on_engine_error(self, err_msg):
        self.update_log(f"엔진 오류 발생: {err_msg}", "#E74C3C")
        messagebox.showerror("엔진 연결 실패", f"엔진과 통신할 수 없습니다:\n{err_msg}\n(실행 파일 이름과 경로를 확인해주세요)")

    def send_cmd(self, cmd):
        if self.engine and self.engine.poll() is None:
            try:
                self.engine.stdin.write(cmd + "\n")
                self.engine.stdin.flush()
            except BrokenPipeError:
                self.on_engine_error("파이프 통신 끊어짐")

    def restart_app(self):
        if self.engine:
            self.engine.kill()
            self.engine.wait()
            self.engine = None
        self.selection_frame.tkraise()

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
                
                color = "#BDC3C7"
                if self.selected_pos == (r, c): color = "#F1C40F"
                elif self.selected_pos:
                    sr, sc = self.selected_pos
                    dist = max(abs(sr-r), abs(sc-c))
                    if dist == 1 and self.board[r][c] == 0: color = "#2ECC71"
                    elif dist == 2 and self.board[r][c] == 0: color = "#3498DB"
                        
                self.canvas.create_rectangle(x0, y0, x1, y1, fill=color, outline="#7F8C8D", width=2)
                
                val = self.board[r][c]
                if val != 0:
                    pc = "#000000" if val == 1 else "#FFFFFF"
                    self.canvas.create_oval(x0+10, y0+10, x1-10, y1-10, fill=pc, outline="#2C3E50", width=2)
        
        self.canvas.update_idletasks()
        
        b_cnt = sum(row.count(1) for row in self.board)
        w_cnt = sum(row.count(2) for row in self.board)
        
        self.info_label.config(text=f"흑(B): {b_cnt}  |  백(W): {w_cnt}", fg="#2C3E50")
        
        b_can_move = self.has_valid_moves(1)
        w_can_move = self.has_valid_moves(2)

        if b_cnt == 0 or w_cnt == 0 or b_cnt + w_cnt == 49 or (not b_can_move and not w_can_move):
            self.game_over = True
            winner = "흑(Black)" if b_cnt > w_cnt else ("백(White)" if w_cnt > b_cnt else "무승부")
            self.info_label.config(text=f"🏁 게임 종료! 승리: {winner}\n(흑: {b_cnt} vs 백: {w_cnt})", fg="#C0392B")
            self.update_log("게임이 종료되었습니다.", "#C0392B")

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
        if not self.engine or self.current_turn == self.engine_color or self.game_over: return
            
        c, r = event.x // 80, event.y // 80
        
        if self.selected_pos:
            sr, sc = self.selected_pos
            if sr == r and sc == c: self.selected_pos = None
            else:
                dist = max(abs(sr-r), abs(sc-c))
                if dist <= 2 and self.board[r][c] == 0:
                    self.apply_move_logic(sr, sc, r, c, self.human_color)
                    self.selected_pos = None
                    self.draw_board()
                    
                    self.update_log("상대방 수 C++ 엔진에 전송 중...", "#34495E")
                    self.send_cmd(f"OPP {sr} {sc} {r} {c}")
                    self.engine.stdout.readline()
                    
                    self.current_turn = self.engine_color
                    self.start_ai_turn()
                elif self.board[r][c] == self.human_color:
                    self.selected_pos = (r, c)
        else:
            if self.board[r][c] == self.human_color: self.selected_pos = (r, c)
        self.draw_board()

    def start_ai_turn(self):
        if self.game_over: return
        self.update_log("🧠 AI 엔진 깊은 수읽기 중... (약 3~5초 소요)", "#C0392B")
        self.draw_board()
        threading.Thread(target=self.wait_for_ai, daemon=True).start()

    def wait_for_ai(self):
        self.send_cmd("THINK")
        resp = self.engine.stdout.readline().strip()
        
        if not resp:
            self.after(0, self.on_engine_error, "AI 연산 중 엔진 강제 종료됨")
            return
            
        if resp.startswith("BESTMOVE"):
            _, fr, fc, tr, tc, isClone = resp.split()
            fr, fc, tr, tc = map(int, [fr, fc, tr, tc])
            
            self.after(0, self.finish_ai_turn, fr, fc, tr, tc)

    def finish_ai_turn(self, fr, fc, tr, tc):
        if fr != -1:
            self.apply_move_logic(fr, fc, tr, tc, self.engine_color)
            
        self.current_turn = self.human_color
        self.draw_board()
        if self.game_over: return
        
        self.update_log("👤 플레이어(인간)의 턴입니다. 돌을 옮겨주세요.", "#27AE60")
        
        if self.current_turn == self.human_color and not self.has_valid_moves(self.human_color):
            self.update_log("플레이어 이동 불가: 자동 턴 패스", "#E74C3C")
            self.after(1500, self.force_human_pass)

    def force_human_pass(self):
        self.send_cmd("OPP -1 -1 -1 -1")
        self.engine.stdout.readline()
        self.current_turn = self.engine_color
        self.start_ai_turn()

if __name__ == "__main__":
    try:
        # [핵심] 현재 운영체제가 윈도우인지 맥인지 알아서 판별하여 파일 이름을 자동으로 맞춥니다.
        engine_file = "algorithm_final2.exe" if sys.platform == "win32" else "./algorithm_final2"
        
        app = AtaxxGUI(engine_file)
        app.mainloop()
    except Exception as e:
        print(f"오류 발생: {e}")

