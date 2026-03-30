CC = gcc
CFLAGS = -Wall -Wextra -std=c99
LDFLAGS = -lSDL2

all: chess

chess: chess.c chess_logic.c
	${CC} ${CFLAGS} -o chess $^

clean:
	rm -f chess
