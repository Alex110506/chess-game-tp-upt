#ifndef CHESS_AUTH_H
#define CHESS_AUTH_H

#include <stdbool.h>

// Modul de autentificare: client HTTP simplu (libcurl) pentru backend-ul FastAPI
// care gestioneaza inregistrare/login/profil/rank si raporteaza rezultatele
// jocurilor multiplayer pentru actualizarea ELO.
//
// Toate apelurile sunt sincrone si blocheaza UI-ul pentru cateva secunde; sunt
// folosite la actiuni explicite (apasare buton), nu in bucla principala.

#define AUTH_USERNAME_MAX 32
#define AUTH_TOKEN_MAX    96
#define AUTH_ERR_MAX     128

typedef struct {
    bool logged_in;
    char username[AUTH_USERNAME_MAX];
    char token[AUTH_TOKEN_MAX];
    int  wins;
    int  losses;
    int  ties;
    int  rank;
    char subscription[16]; // "free" | "pro" | "cancelling"
} AuthState;

extern AuthState gAuth;

// URL-ul de baza al backend-ului (fara slash final). Implicit:
// http://127.0.0.1:8765 — modificabil prin variabila de mediu CHESS_BACKEND_URL.
const char *auth_server_url(void);

// Persistenta sesiunii intr-un fisier local (token + username) astfel incat
// utilizatorul sa ramana logat intre porniri.
void auth_load_session(void);
void auth_save_session(void);
void auth_clear_session(void);

// Operatiuni — returneaza 1 la succes, 0 la esec. err_buf (poate fi NULL)
// primeste mesajul de eroare returnat de server / motiv local.
int  auth_register(const char *username, const char *password,
                   char *err_buf, int err_sz);
int  auth_login(const char *username, const char *password,
                char *err_buf, int err_sz);
int  auth_refresh_profile(char *err_buf, int err_sz); // GET /me
void auth_logout(void);

// Raporteaza rezultatul unui joc multiplayer. result: "win" | "loss" | "draw".
// opponent poate fi NULL/"" (ranking-ul oponentului nu va fi recalculat).
int  auth_report_game(const char *result, const char *opponent,
                      char *err_buf, int err_sz);

// Initializare/curatare libcurl (apelate o data la pornire/inchidere).
void auth_init(void);
void auth_cleanup(void);

#endif // CHESS_AUTH_H
