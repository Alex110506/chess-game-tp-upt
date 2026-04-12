#ifndef CHESS_LOGIC_H
#define CHESS_LOGIC_H

#define EMPTY '.'

extern char board[8][8];
extern int current_turn; //0 if white, 1 if black
extern int ep_target_row, ep_target_col;

//initalization and state of the game
void init_board(void);
int is_white(char p);
int is_black(char p);
int is_own(char p, int turn);
int is_enemy(char p, int turn);
void get_king_pos(int turn, int *kr, int *kc);

//validation of moves
int pseudo_legal(int r1, int c1, int r2, int c2, int turn);
int try_move_legal(int r1, int c1, int r2, int c2, int turn);
int is_in_check(int turn);
int has_legal_moves(int turn);

//promotion piece e 0 in cazul in care nu e necesar, altfel se inlocuieste cu piesa la input
void execute_move(int r1, int c1, int r2, int c2, char promotion_piece);

//genereaza FEN-ul pozitiei curente (pentru comunicare cu engine-ul)
void board_to_fen(char *fen, int max_len);

#endif