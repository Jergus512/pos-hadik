CC=clang
CFLAGS=-std=c11 -Wall -Wextra -Wpedantic -Werror -g -Iinclude
BIN_DIR=build
SRC_DIR=src

COMMON_SRC=$(SRC_DIR)/ipc.c $(SRC_DIR)/world.c $(SRC_DIR)/snake.c $(SRC_DIR)/game.c $(SRC_DIR)/util.c
CLIENT_SRC=$(SRC_DIR)/client_main.c $(SRC_DIR)/render.c $(SRC_DIR)/input.c
SERVER_SRC=$(SRC_DIR)/server_main.c

client: $(BIN_DIR) $(COMMON_SRC) $(CLIENT_SRC)
	$(CC) $(CFLAGS) -o $(BIN_DIR)/client $(COMMON_SRC) $(CLIENT_SRC)

server: $(BIN_DIR) $(COMMON_SRC) $(SERVER_SRC)
	$(CC) $(CFLAGS) -o $(BIN_DIR)/server $(COMMON_SRC) $(SERVER_SRC)

all: client server

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -rf $(BIN_DIR)

