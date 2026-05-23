import re

with open("chess_coach.c", "r") as f:
    content = f.read()

# 1. CoachJobInputs
struct_pattern = r'(typedef struct \{\n    char  fen\[160\];\n    char  last_move\[16\];\n    char  side\[8\];\n    char  difficulty\[16\];\n    char  player_color\[8\];\n)(\} CoachJobInputs;)'
struct_repl = r'\1    char  best_move[8];\n\2'
content = re.sub(struct_pattern, struct_repl, content)

# 2. build_payload
payload_pattern = r'(    if \(in->player_color\[0\]\) \{\n        APPEND_LIT\(",\\"player_color\\":\\""\);\n        if \(\!json_append_escaped\(&p, end, in->player_color\)\) \{ free\(buf\); return NULL; \}\n        APPEND_LIT\("\\""\);\n    \})'
payload_repl = r'\1\n    if (in->best_move[0]) {\n        APPEND_LIT(",\\"best_move\\":\\"");\n        if (!json_append_escaped(&p, end, in->best_move)) { free(buf); return NULL; }\n        APPEND_LIT("\\"");\n    }'
content = re.sub(payload_pattern, payload_repl, content)

# 3. coach_send
coach_send_pattern = r'(bool coach_send\(const char \*user_text,\n                const char \*fen,\n                const char \*last_move,\n                const char \*side_to_move,\n                const char \*difficulty,\n                const char \*player_color\))'
coach_send_repl = r'bool coach_send(const char *user_text,\n                const char *fen,\n                const char *last_move,\n                const char *side_to_move,\n                const char *difficulty,\n                const char *player_color,\n                const char *best_move)'
content = re.sub(coach_send_pattern, coach_send_repl, content)

# 4. coach_send body setting best_move
body_pattern = r'(    if \(player_color\)  snprintf\(in\.player_color, sizeof\(in\.player_color\), "%s", player_color\);)'
body_repl = r'\1\n    if (best_move)     snprintf(in.best_move,    sizeof(in.best_move),    "%s", best_move);'
content = re.sub(body_pattern, body_repl, content)

with open("chess_coach.c", "w") as f:
    f.write(content)
