#include "ipc.h"
#include "protocol.h"

#include <stdio.h>
#include <unistd.h>

int main(void) {
    int fd = ipc_client_connect(SNAKE_SOCK_PATH);
    printf("[client] Connected to server\n");

    // PING
    msg_cmd_t ping = {.cmd = CMD_PING};
    ipc_send_all(fd, &ping, sizeof(ping));

    msg_resp_t resp;
    ipc_recv_all(fd, &resp, sizeof(resp));
    printf("[client] Response to PING: %d (expect %d)\n", resp.resp, RESP_PONG);

    // QUIT
    msg_cmd_t quit = {.cmd = CMD_QUIT};
    ipc_send_all(fd, &quit, sizeof(quit));
    ipc_recv_all(fd, &resp, sizeof(resp));
    printf("[client] Response to QUIT: %d (expect %d)\n", resp.resp, RESP_BYE);

    close(fd);
    return 0;
}

