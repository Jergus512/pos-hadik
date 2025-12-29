#include "ipc.h"
#include "protocol.h"

#include <stdio.h>
#include <unistd.h>

int main(void) {
    int listen_fd = ipc_server_listen(SNAKE_SOCK_PATH);
    printf("[server] Listening on %s\n", SNAKE_SOCK_PATH);

    int fd = ipc_server_accept(listen_fd);
    printf("[server] Client connected\n");

    for (;;) {
        msg_cmd_t cmd;
        if (ipc_recv_all(fd, &cmd, sizeof(cmd)) != 0) {
            printf("[server] Client disconnected\n");
            break;
        }

        if (cmd.cmd == CMD_PING) {
            msg_resp_t resp = {.resp = RESP_PONG};
            ipc_send_all(fd, &resp, sizeof(resp));
            printf("[server] PING -> PONG\n");
        } else if (cmd.cmd == CMD_QUIT) {
            msg_resp_t resp = {.resp = RESP_BYE};
            ipc_send_all(fd, &resp, sizeof(resp));
            printf("[server] QUIT -> BYE, shutting down\n");
            break;
        } else {
            printf("[server] Unknown cmd: %d\n", cmd.cmd);
        }
    }

    close(fd);
    close(listen_fd);
    unlink(SNAKE_SOCK_PATH);
    return 0;
}

