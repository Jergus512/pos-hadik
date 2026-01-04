CC ?= cc

CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -g -Iinclude -pthread
LDLIBS_CLIENT ?= -lncurses

BUILD_DIR := build

COMMON_SRC := src/ipc.c src/world.c src/snake.c src/game.c src/util.c
CLIENT_SRC := $(COMMON_SRC) src/client_main.c src/render.c src/input.c
SERVER_SRC := $(COMMON_SRC) src/server_main.c

.PHONY: all client server clean

all: client server

client:
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $(BUILD_DIR)/client $(CLIENT_SRC) $(LDLIBS_CLIENT)

server:
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $(BUILD_DIR)/server $(SERVER_SRC)

clean:
	rm -rf $(BUILD_DIR)
