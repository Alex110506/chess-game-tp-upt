#ifndef CHESS_LOGIC_H
#define CHESS_LOGIC_H

// caracterul folosit pentru a reprezenta un patrat gol pe tabla
#define EMPTY '.'

// tabla de sah (8x8) stocata ca o matrice de caractere
extern char board[8][8];

// starea randului curent: 0 daca muta albul, 1 daca muta negrul
extern int current_turn; 

// coordonatele pentru mutarea "en passant" (-1 daca nu exista)
extern int ep_target_row, ep_target_col;

// initializare si starea jocului
void init_board(void); // plaseaza piesele in pozitia de start
int is_white(char p);  // returneaza 1 daca piesa apartine albului
int is_black(char p);  // returneaza 1 daca piesa apartine negrului
int is_own(char p, int turn);   // returneaza 1 daca piesa apartine jucatorului curent
int is_enemy(char p, int turn); // returneaza 1 daca piesa apartine oponentului
void get_king_pos(int turn, int *kr, int *kc); // gaseste coordonatele regelui pe tabla

// validarea mutarilor
// verifica daca o mutare respecta regulile de baza ale piesei (fara a lua in considerare sahul)
int pseudo_legal(int r1, int c1, int r2, int c2, int turn);
// verifica daca mutarea este legala complet (inclusiv nu iti pune propriul rege in sah)
int try_move_legal(int r1, int c1, int r2, int c2, int turn);
// verifica daca regele jucatorului "turn" este atacat (este in sah)
int is_in_check(int turn);
// returneaza 1 daca jucatorul curent are macar o mutare legala posibila
int has_legal_moves(int turn);

// executa mutarea pe tabla. "promotion_piece" trebuie sa fie caracterul noii piese in caz de promovare, sau orice alt caracter daca nu e promovare
void execute_move(int r1, int c1, int r2, int c2, char promotion_piece);

// genereaza string-ul FEN al pozitiei curente (pentru comunicarea starii cu engine-ul Stockfish)
void board_to_fen(char *fen, int max_len);

#endif