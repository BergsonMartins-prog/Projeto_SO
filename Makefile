CC=gcc
CFLAGS=-Wall -Iinclude
SRC=$(wildcard src/*.c)
OBJ=$(SRC:.c=.o)
BIN=simulador

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $(BIN)
	@rm -f $(OBJ)

clean:
	rm -f $(OBJ) $(BIN)
