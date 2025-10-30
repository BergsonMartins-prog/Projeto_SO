CC=gcc
CFLAGS=-Wall -Iinclude
SRC=$(wildcard src/*.c)
OBJ=$(SRC:.c=.o)
BIN=simulador

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $(BIN)

clean:
	rm -f $(OBJ) $(BIN)
