CC      := cc
CFLAGS  := -std=c11 -Wall -Wextra -Wpedantic -Werror -g -Iinclude -pthread
LDFLAGS :=
LDLIBS_CLIENT := -lncurses

BUILD := build
CLIENT := $(BUILD)/client
SERVER := $(BUILD)/server

CLIENT_SRC := src/ipc.c src/client_main.c
SERVER_SRC := src/ipc.c src/server_main.c

.PHONY: all clean client server

all: client server

$(BUILD):
	mkdir -p $(BUILD)

client: $(BUILD)
	$(CC) $(CFLAGS) -o $(CLIENT) $(CLIENT_SRC) $(LDFLAGS) $(LDLIBS_CLIENT)

server: $(BUILD)
	$(CC) $(CFLAGS) -o $(SERVER) $(SERVER_SRC) $(LDFLAGS)

clean:
	rm -rf $(BUILD)
