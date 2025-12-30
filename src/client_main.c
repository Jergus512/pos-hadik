#include "ipc.h"
#include "protocol.h"

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

typedef struct {
    int fd;
    int running;
} client_state_t;

static void *recv_thread(void *arg) {
    client_state_t *st = (client_state_t *)arg;

    while (st->running) {
        msg_resp_t hdr;
        if (ipc_recv_all(st->fd, &hdr, sizeof(hdr)) != 0) {
            printf("[client] server closed connection\n");
            break;
        }

        if (hdr.resp == RESP_PONG) {
            printf("[client] got PONG\n");
        } else if (hdr.resp == RESP_BYE) {
            printf("[client] got BYE\n");
            break;
        } else if (hdr.resp == RESP_TICK) {
            msg_tick_t t;
            if (ipc_recv_all(st->fd, &t, sizeof(t)) != 0) break;
            printf("[client] tick=%d\n", t.tick);
        } else {
            printf("[client] unknown resp=%d\n", hdr.resp);
        }
        fflush(stdout);
    }

    st->running = 0;
    return NULL;
}

int main(void) {
    int fd = ipc_client_connect(SNAKE_SOCK_PATH);
    printf("[client] Connected\n");

    client_state_t st;
    st.fd = fd;
    st.running = 1;

    pthread_t th_recv;
    if (pthread_create(&th_recv, NULL, recv_thread, &st) != 0) {
        perror("pthread_create(recv)");
        return 1;
    }

    msg_cmd_t ping = {.cmd = CMD_PING};
    (void)ipc_send_all(fd, &ping, sizeof(ping));

    sleep(1);

    msg_cmd_t quit = {.cmd = CMD_QUIT};
    (void)ipc_send_all(fd, &quit, sizeof(quit));

    pthread_join(th_recv, NULL);

    close(fd);
    printf("[client] exit\n");
    return 0;
}
