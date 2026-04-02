CC = gcc
CFLAGS = -Wall -Wextra -std=c99
LDFLAGS = -lSDL2

# Raylib flags – tries pkg-config first, falls back to common Homebrew paths
RAYLIB_FLAGS := $(shell pkg-config --libs --cflags raylib 2>/dev/null)
ifeq ($(RAYLIB_FLAGS),)
  RAYLIB_FLAGS := -I/opt/homebrew/include -L/opt/homebrew/lib \
                  -lraylib \
                  -framework OpenGL -framework Cocoa -framework IOKit \
                  -framework CoreAudio -framework CoreVideo
endif

all: chess

chess: chess.c chess_logic.c
	${CC} ${CFLAGS} -o chess $^

gui: chess_gui.c chess_logic.c
	${CC} ${CFLAGS} -o chess_gui $^ ${RAYLIB_FLAGS}

clean:
	rm -f chess chess_gui
