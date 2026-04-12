#include <stdlib.h>
#include <ctype.h>
#include "chess_logic.h"

char board[8][8];
int current_turn; //0 rand alb, 1 rand negru

// starea pentru rocada
//starea regilor si a turelor, daca vreuna e mutata nu se mai poate face rocada
static int white_king_moved, black_king_moved;
static int white_rook_a_moved, white_rook_h_moved;
static int black_rook_a_moved, black_rook_h_moved;

//starea pentru en passant
//urmareste coordonatele unde un pion poate fi capturat in en passant strict la urmatoarea mutare
int ep_target_row, ep_target_col;


//verificari piese
int is_white(char p){
    return p!=EMPTY && isupper(p);
}
int is_black(char p){
    return p!=EMPTY && islower(p);
}
int is_own(char p, int turn){
    return (turn == 0 && isupper(p)) || (turn == 1 && islower(p));
}
int is_enemy(char p, int turn){
    return (turn == 1 && isupper(p)) || (turn == 0 && islower(p));
}
static int in_bounds(int row, int col) { 
    return row >= 0 && row < 8 && col >= 0 && col < 8; 
}

//initializam tabla
void init_board(){
    // R = Turn, N = Cal, B = Nebun, Q = Regina, K = Rege
    char s[9]="RNBQKBNR";
    for (int i=0 ; i<8 ; i++){
        board[0][i]=tolower(s[i]);
        board[1][i]='p';
        
        board[7][i]=s[i];
        board[6][i]='P';

        for(int j=2; j<6 ; j++){
            board[j][i]=EMPTY;
        }
    }

    white_king_moved=black_king_moved=0;
    white_rook_a_moved=black_rook_a_moved=0;
    white_rook_h_moved=black_rook_h_moved=0;

    current_turn=0;// alb muta primul
}

//verificam daca exista piese in calea drumului dorit
static int is_path_clear_straight(int r1, int c1, int r2, int c2){
    int dr = (r2 > r1) - (r2 < r1); // -1 -stanga ; 0 -nimic ; 1 -dreapta
    int dc = (c2 > c1) - (c2 < c1); // -1 -sus ; 0 -nimic ; 1 -jos
    int r = r1 + dr, c = c1 + dc;

    // se parcurge fiecare casuta
    while (r != r2 || c != c2) {
        if (board[r][c] != EMPTY) return 0;
        r += dr; c += dc;
    }
    return 1; //drumul e liber
}

static int is_sqare_attacked(int r, int c, int by_turn){
    //cauta pe tabla piesele adversarului
    for (int rr = 0; rr < 8; rr++)
        for (int cc = 0; cc < 8; cc++) {
            char p = board[rr][cc];
            if (!is_own(p, by_turn)) continue; //treci peste daca e piesa de a ta
            
            char P = toupper(p);
            int dr = r - rr, dc = c - cc; // distanta in coordonate
            int dir;
            
            switch (P) {
            case 'P': //pioni
                dir = (by_turn == 0) ? -1 : 1; // albul ataca in sus is negrul in jos
                if (dr == dir && abs(dc) == 1) return 1;
                break;
            case 'N': //cal
                if ((abs(dr) == 2 && abs(dc) == 1) || (abs(dr) == 1 && abs(dc) == 2)) return 1;
                break;
            case 'B': // nebun
                if (abs(dr) == abs(dc) && dr != 0 && is_path_clear_straight(rr, cc, r, c)) return 1;
                break;
            case 'R': // tura
                if (((dr == 0 && dc != 0) || (dc == 0 && dr != 0)) && is_path_clear_straight(rr, cc, r, c)) return 1;
                break;
            case 'Q': // regina
                if ((dr == 0 || dc == 0 || abs(dr) == abs(dc)) && (dr != 0 || dc != 0) && is_path_clear_straight(rr, cc, r, c)) return 1;
                break;
            case 'K': // regele
                if (abs(dr) <= 1 && abs(dc) <= 1 && (dr != 0 || dc != 0)) return 1;
                break;
            }
        }
    return 0; // esti safe
}   

void get_king_pos(int turn, int *kr, int *kc){
    
    char king= (turn==0) ? 'K' : 'k';

    for(int i=0 ; i<8 ; i++){
        for(int j=0 ; j<8 ; j++){
            if(board[i][j]==king){
                *kr=i;
                *kc=j;
                return;
            }
        }
    }

    *kr=*kc=-1; //valori daca nu a fost gasit
    return;
}

//verificam sah
int is_in_check(int turn){
    int kr,kc;
    get_king_pos(turn,&kr,&kc);
    if(kr<0) return 0;
    return is_sqare_attacked(kr,kc,1-turn);
}

//verifica daca piesa respecta regulile sale
int pseudo_legal(int r1, int c1, int r2, int c2, int turn)
{
    if (!in_bounds(r1, c1) || !in_bounds(r2, c2)) return 0;
    if (r1 == r2 && c1 == c2) return 0;

    char piece = board[r1][c1];
    if (!is_own(piece, turn)) return 0;

    char target = board[r2][c2];
    if (is_own(target, turn)) return 0;

    char P = toupper(piece);
    int dr = r2 - r1, dc = c2 - c1;

    // reguli pt fiecare piesa
    switch (P) {
    case 'P': {
        int dir = (turn == 0) ? -1 : 1;
        int start_row = (turn == 0) ? 6 : 1;
        // misacre normala de un partat
        if (dc == 0 && dr == dir && target == EMPTY) return 1;
        // miscare cu 2 patrate din pozitie initiala
        if (dc == 0 && dr == 2 * dir && r1 == start_row && target == EMPTY && board[r1 + dir][c1] == EMPTY) return 1;
        // capturare pe diagonala
        if (abs(dc) == 1 && dr == dir) {
            if (is_enemy(target, turn)) return 1; 
            if (r2 == ep_target_row && c2 == ep_target_col) return 1; // en passant
        }
        return 0;
    }
    case 'N': return (abs(dr) == 2 && abs(dc) == 1) || (abs(dr) == 1 && abs(dc) == 2);
    case 'B': return abs(dr) == abs(dc) && is_path_clear_straight(r1, c1, r2, c2);
    case 'R': return (dr == 0 || dc == 0) && is_path_clear_straight(r1, c1, r2, c2);
    case 'Q': return (dr == 0 || dc == 0 || abs(dr) == abs(dc)) && is_path_clear_straight(r1, c1, r2, c2);
    case 'K': {
        if (abs(dr) <= 1 && abs(dc) <= 1) return 1; //mutare normala de rege
        
        // logica pt rocada (cand regele se muta 2 patratele spre turn)
        if (dr == 0 && abs(dc) == 2) {
            if (turn == 0 && white_king_moved) return 0;
            if (turn == 1 && black_king_moved) return 0;
            int row = (turn == 0) ? 7 : 0;
            if (r1 != row || c1 != 4) return 0; // regele trebuie sa fie pe locul de start
            if (is_in_check(turn)) return 0; // nu poti face rocada daca esti in sah

            if (dc == 2) { //rocada mica pe partea regelui
                if (turn == 0 && white_rook_h_moved) return 0;
                if (turn == 1 && black_rook_h_moved) return 0;
                char expected_rook = (turn == 0) ? 'R' : 'r';
                if (board[row][7] != expected_rook) return 0;
                if (board[row][5] != EMPTY || board[row][6] != EMPTY) return 0; //spatiul dintre ele trebuie sa fie gol
                if (is_sqare_attacked(row, 5, 1 - turn)) return 0; //regele nu poate trece prin sah
                if (is_sqare_attacked(row, 6, 1 - turn)) return 0; //regele nu poate ajunge in sah
                return 1;
            }
            if (dc == -2) { //rocada mare pe partea reginei
                if (turn == 0 && white_rook_a_moved) return 0;
                if (turn == 1 && black_rook_a_moved) return 0;
                char expected_rook = (turn == 0) ? 'R' : 'r';
                if (board[row][0] != expected_rook) return 0;
                if (board[row][1] != EMPTY || board[row][2] != EMPTY || board[row][3] != EMPTY) return 0;
                if (is_sqare_attacked(row, 3, 1 - turn)) return 0;
                if (is_sqare_attacked(row, 2, 1 - turn)) return 0;
                return 1;
            }
        }
        return 0;
    }
    }
    return 0;
}

//simuleaza o mutare pentru a verifica daca la finalul ei regele este in siguranta (nu e in sah)
int try_move_legal(int r1, int c1, int r2, int c2, int turn){
    char src=board[r1][c1];
    char dst=board[r2][c2];
    char ep_captured=EMPTY;
    int ep_cap_r=-1, ep_cap_c=-1;

    //simuleaza capturarea en passant(elimina pionul inamic temporar)
    if(toupper(src)=='P' && c2!=c1 && dst==EMPTY){
        ep_cap_r=r1;
        ep_cap_c=c2;
        ep_captured=board[ep_cap_r][ep_cap_c];
        board[ep_cap_r][ep_cap_c]=EMPTY;
    }

    //simuleaza miscarea turei in caz de rocada
    char castle_rook = EMPTY;
    int rook_src_c = -1, rook_dst_c = -1;
    if (toupper(src) == 'K' && abs(c2 - c1) == 2) {
        int row = r1;
        if (c2 - c1 == 2) { 
            rook_src_c = 7; 
            rook_dst_c = 5; 
        } //rocada mica
        else{ 
            rook_src_c = 0; 
            rook_dst_c = 3; 
        } //rocada mare
        castle_rook = board[row][rook_src_c];
        board[row][rook_dst_c] = castle_rook;
        board[row][rook_src_c] = EMPTY;
    }

    //face mutarea simulata pe tabla
    board[r2][c2] = src;
    board[r1][c1] = EMPTY;

    //verifica daca regele a ramas in siguranta (1 = legal, 0 = ilegal)
    int legal = !is_in_check(turn);

    //anuleaza mutarea simulata (pune tabla la loc exact cum era)
    board[r1][c1] = src;
    board[r2][c2] = dst;

    if (ep_cap_r >= 0) board[ep_cap_r][ep_cap_c] = ep_captured;
    if (rook_src_c >= 0) {
        int row = r1;
        board[row][rook_src_c] = castle_rook;
        board[row][rook_dst_c] = EMPTY;
    }

    return legal;
}

//verifica daca jucatorul curent are vreo mutare legala ramasa
//(detecteaza sah-mat sau pat)
int has_legal_moves(int turn)
{
    for (int r1 = 0; r1 < 8; r1++)
        for (int c1 = 0; c1 < 8; c1++) {
            if (!is_own(board[r1][c1], turn)) continue;
            for (int r2 = 0; r2 < 8; r2++)
                for (int c2 = 0; c2 < 8; c2++) {
                    if (pseudo_legal(r1, c1, r2, c2, turn) && try_move_legal(r1, c1, r2, c2, turn))
                        return 1; // a gasit cel putin o mutare valida
                }
        }
    return 0; // nu mai exista mutari legale
}

//executare mutare oficiala pe tabla (mutarea inseamna ca a fost deja validata)
void execute_move(int r1, int c1, int r2, int c2, char promotion){
    char piece=board[r1][c1];
    char P=toupper(piece);

    //gestioneaza caputarea en passant pe bune
    if(P=='P' && c2!=c1 && board[r2][c2]==EMPTY){
        board[r1][c2]=EMPTY;//sterge pionul capturat
    }

    //gestioneaza mutarea turei la rocada
    if(P=='K' && abs(c2-c1)==2){
        int row=r1;
        if(c2-c1==2){ //rocada mica
            board[row][5]=board[row][7];
            board[row][7]=EMPTY;
        }else{//rocada mare
            board[row][3]=board[row][0];
            board[row][0]=EMPTY;
        }
    }

    //muta piesa propriu zisa
    board[r2][c2] = piece;
    board[r1][c1] = EMPTY;

    // seteaza tinta pentru en passant daca pionul a facut un salt dublu
    if (P == 'P' && abs(r2 - r1) == 2) {
        ep_target_row = (r1 + r2) / 2;
        ep_target_col = c1;
    } else {
        ep_target_row = ep_target_col = -1; //reseteaza daca nu e cazul
    }

    //inregistreaza permanent miscarea regilor si turnurilor (anuleaza rocada pe viito)
    if(piece=='K') white_king_moved=1;
    if(piece=='k') black_king_moved=1;
    if(piece=='r' || piece=='R'){
        if(r1==7 && c1==0) white_rook_a_moved=1;
        if(r1==7 && c1==7) white_rook_h_moved=1;
        if(r1==0 && c1==0) black_rook_a_moved=1;
        if(r1==0 && c1==7) black_rook_h_moved=1;
    }

    //daca un turn este capturat pe pozitia initiala, anuleaza rocada pentru acea parte
    if (r2 == 7 && c2 == 0) white_rook_a_moved = 1;
    if (r2 == 7 && c2 == 7) white_rook_h_moved = 1;
    if (r2 == 0 && c2 == 0) black_rook_a_moved = 1;
    if (r2 == 0 && c2 == 7) black_rook_h_moved = 1;

    //trasformarea pionului (promovare) - cand ajunge la ultimul rand
    if (P=='P' && (r2==0 || r2==7) && promotion!=0){
        board[r2][c2] = (current_turn == 0) ? (char)toupper(promotion) : (char)tolower(promotion);
    }
}

//genereaza FEN-ul pozitiei curente
#include <stdio.h>
void board_to_fen(char *fen, int max_len){
    int idx = 0;

    //1. pozitia pieselor
    for (int r = 0; r < 8; r++){
        int empty = 0;
        for (int c = 0; c < 8; c++){
            if (board[r][c] == EMPTY){
                empty++;
            } else {
                if (empty > 0){ fen[idx++] = '0' + empty; empty = 0; }
                fen[idx++] = board[r][c];
            }
        }
        if (empty > 0) fen[idx++] = '0' + empty;
        if (r < 7) fen[idx++] = '/';
    }

    //2. culoarea activa
    fen[idx++] = ' ';
    fen[idx++] = (current_turn == 0) ? 'w' : 'b';

    //3. drepturi de rocada
    fen[idx++] = ' ';
    int ci = idx;
    if (!white_king_moved){
        if (!white_rook_h_moved) fen[idx++] = 'K';
        if (!white_rook_a_moved) fen[idx++] = 'Q';
    }
    if (!black_king_moved){
        if (!black_rook_h_moved) fen[idx++] = 'k';
        if (!black_rook_a_moved) fen[idx++] = 'q';
    }
    if (idx == ci) fen[idx++] = '-';

    //4. en passant
    fen[idx++] = ' ';
    if (ep_target_row >= 0 && ep_target_col >= 0){
        fen[idx++] = (char)('a' + ep_target_col);
        fen[idx++] = (char)('0' + (8 - ep_target_row));
    } else {
        fen[idx++] = '-';
    }

    //5. halfmove si fullmove (simplificat)
    fen[idx++] = ' ';
    fen[idx++] = '0';
    fen[idx++] = ' ';
    fen[idx++] = '1';
    fen[idx++] = '\0';
    (void)max_len;
}