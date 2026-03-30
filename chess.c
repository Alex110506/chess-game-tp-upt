#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "chess_logic.h"

#define RESET "\033[0m"
#define BOLD "\033[1m"
#define BG_LIGHT "\033[48;5;223m"
#define BG_DARK "\033[48;5;137m"
#define FG_WHITE "\033[38;5;255m"
#define FG_BLACK "\033[38;5;232m"
#define FG_LABEL "\033[38;5;245m"
#define FG_TITLE "\033[38;5;214m"
#define FG_INFO "\033[38;5;117m"
#define FG_ERR "\033[38;5;203m"
#define FG_OK "\033[38;5;114m"

//returneaza simboluri unicode corespunzatoare pieselor date prin char
static const char *piece_symbol(char p)
{
    switch (p) {
        case 'K': return "♔"; case 'Q': return "♕"; case 'R': return "♖";
        case 'B': return "♗"; case 'N': return "♘"; case 'P': return "♙";
        case 'k': return "♚"; case 'q': return "♛"; case 'r': return "♜";
        case 'b': return "♝"; case 'n': return "♞"; case 'p': return "♟";
        default:  return " ";
    }
}

//printeaza tabla
static void print_board(void)
{
    printf("\n");
    printf("  " FG_TITLE BOLD "  ╔═══════════════════════════╗" RESET "\n");
    printf("  " FG_TITLE BOLD "  ║     C H E S S  G A M E    ║" RESET "\n");
    printf("  " FG_TITLE BOLD "  ║        made by alex       ║" RESET "\n");
    printf("  " FG_TITLE BOLD "  ╚═══════════════════════════╝" RESET "\n\n");

    for (int r = 0; r < 8; r++) {
        printf("  " FG_LABEL " %d " RESET, 8 - r);
        for (int c = 0; c < 8; c++) {
            const char *bg = ((r + c) % 2 == 0) ? BG_LIGHT : BG_DARK;
            const char *fg = is_white(board[r][c]) ? FG_WHITE : FG_BLACK;
            printf("%s%s %s " RESET, bg, fg, piece_symbol(board[r][c]));
        }
        printf("\n");
    }
    printf("  " FG_LABEL "    a  b  c  d  e  f  g  h" RESET "\n\n");

    if (current_turn == 0)
        printf("  " FG_INFO BOLD "  ⬜  White to move" RESET "\n");
    else
        printf("  " FG_INFO BOLD "  ⬛  Black to move" RESET "\n");
}

//parseaza coordonate de tip "e2" in indexi de matrice
static int parse_square(const char *s, int *r, int *c)
{
    if (strlen(s) < 2) return 0;
    char file = (char)tolower((unsigned char)s[0]);
    char rank = s[1];
    if (file < 'a' || file > 'h') return 0;
    if (rank < '1' || rank > '8') return 0;
    *c = file - 'a';
    *r = 8 - (rank - '0');
    return 1;
}

int main(void){
    init_board();
    //da clear la temrinal si pune cursorul in stanga sus
    printf("\033[2J\033[H");

    while (1){
        print_board();
        int check=is_in_check(current_turn);

        //verificam daca mai sunt mutari legale pt jucator
        if(!has_legal_moves(current_turn)){
            if(check){
                printf("\n  " FG_OK BOLD "  ★  MAT! Câștigă %s!  ★" RESET "\n\n",current_turn == 0 ? "Negru" : "Alb");
            }else{
                printf("\n  " FG_INFO BOLD "  ═  REMIZĂ (Pat) — joc egal  ═" RESET "\n\n");
            }
            break;
        }

        //in caz de eroare a verificarii
        if (check) printf("  " FG_ERR BOLD "  ⚠  ȘAH!" RESET "\n");
        printf("\n  " FG_LABEL "  Introdu mutarea (ex: e2 e4) sau 'quit': " RESET);
        //flush la buffer
        fflush(stdout);

        //stabilim inputul jucatorului si il parsam
        char line[64];
        if (!fgets(line, sizeof(line), stdin)) break;
        line[strcspn(line, "\n")] = '\0';

        //optiunea de iesire
        if(strcmp(line,"quit") == 0 || strcmp(line,"exit")==0){
            printf("\n  " FG_INFO "  Mulțumim pentru joc! ♟" RESET "\n\n");
            break;
        }

        //initializare mutari
        char from_str[3], to_str[3];
        if (sscanf(line, "%2s %2s", from_str, to_str) != 2) {
            printf("  " FG_ERR "  ✗ Intrare invalidă. Format: e2 e4" RESET "\n");
            continue;
        }

        int r1, c1, r2, c2;
        //validarea pozitiilor introduse
        if (!parse_square(from_str, &r1, &c1) || !parse_square(to_str, &r2, &c2)) {
            printf("  " FG_ERR "  ✗ Pătrat invalid. Folosește a-h și 1-8." RESET "\n");
            continue;
        }

        //verificam daca piesa nu este a jucatorului curent
        if (!is_own(board[r1][c1], current_turn)) {
            printf("  " FG_ERR "  ✗ Nu există piesă %s la %s." RESET "\n",current_turn == 0 ? "albă" : "neagră", from_str);
            continue;
        }
        
        //validare din punct de vedere al piesei
        if (!pseudo_legal(r1, c1, r2, c2, current_turn)) {
            printf("  " FG_ERR "  ✗ Mutare ilegală pentru acea piesă." RESET "\n");
            continue;
        }
        
        //verificam daca mutarea lasa regele propriu in sah
        if (!try_move_legal(r1, c1, r2, c2, current_turn)) {
            printf("  " FG_ERR "  ✗ Această mutare vă lasă regele în șah." RESET "\n");
            continue;
        }

        //gestionarea promovarii pionului
        char promo = 0;
        char P = (char)toupper((unsigned char)board[r1][c1]);
        if (P == 'P' && (r2 == 0 || r2 == 7)) {
            while (1) {
                printf("\n  " FG_OK "  Promovare pion în (Q/R/B/N): " RESET);
                char buf[16];
                if (!fgets(buf, sizeof(buf), stdin)){ 
                    promo = 'Q'; break; //in caz ca nu se ia input
                }
                char ch = (char)toupper((unsigned char)buf[0]);
                if (ch == 'Q' || ch == 'R' || ch == 'B' || ch == 'N') {
                    promo = ch; break;
                }
                printf("  " FG_ERR "  Alegere invalidă. Încearcă din nou." RESET "\n");
            }
        }

        execute_move(r1,c1,r2,c2,promo);
        current_turn=1-current_turn;
        printf("\033[2J\033[H");
    }
    
}

