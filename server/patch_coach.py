import re

with open("coach.py", "r") as f:
    content = f.read()

# 1. Add best_move to CoachRequest
content = re.sub(
    r'(    session_id: Optional\[str\] = Field\(\n        default=None,\n        description="Opaque client-side identifier; used only for server logging."\n    \))',
    r'\1\n    best_move: Optional[str] = Field(\n        default=None, description="The best move evaluated by the client GUI."\n    )',
    content
)

# 2. Remove run_chess_engine
# The block is from "def run_chess_engine(fen: str, top_k: int = 3) -> dict:" to the end of the function.
engine_func_pattern = re.compile(r'def run_chess_engine.*?return \{\n.*?\"suggestions\":  suggestions,\n    \}', re.DOTALL)
content = re.sub(engine_func_pattern, '', content)

# 3. Remove DEFAULT_STOCKFISH_PATH and ENGINE_THINK_SECONDS
content = re.sub(r'DEFAULT_STOCKFISH_PATH = "./stockfish/stockfish"\n', '', content)
content = re.sub(r'ENGINE_THINK_SECONDS   = 0.8     # per engine run\n', '', content)
content = re.sub(r'def _stockfish_path\(\) -> str:\n    return os.environ.get\("STOCKFISH_PATH", DEFAULT_STOCKFISH_PATH\)\n\n\n', '', content)

# 4. Modify coach_chat
coach_chat_pattern = re.compile(r'    engine_result = None\n    # Always get the suggested move from Stockfish if the player could be asking about it\.\n    if side == pc:\n        engine_result = await asyncio\.to_thread\(run_chess_engine, body\.fen, 1\)')
coach_chat_replacement = '''    engine_result = None
    if side == pc and body.best_move:
        engine_result = {"suggestions": [{"uci": body.best_move}]}'''
content = re.sub(coach_chat_pattern, coach_chat_replacement, content)

# 5. Modify coach_chat_stream
coach_chat_stream_pattern = re.compile(r'    engine_result = None\n    if side == pc:\n        engine_result = await asyncio\.to_thread\(run_chess_engine, body\.fen, 1\)')
coach_chat_stream_replacement = '''    engine_result = None
    if side == pc and body.best_move:
        engine_result = {"suggestions": [{"uci": body.best_move}]}'''
content = re.sub(coach_chat_stream_pattern, coach_chat_stream_replacement, content)

with open("coach.py", "w") as f:
    f.write(content)
