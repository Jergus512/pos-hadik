#include "ipc.h"
#include "protocol.h"

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

typedef struct {
    int fd;
    pthread_mutex_t lock;
    int running;
    int tick;
} server_state_t;

static void *game_loop_thread(void *arg) {
    server_state_t *st = (server_state_t *)arg;

    for (;;) {
        usleep(100000); // 100 ms

        pthread_mutex_lock(&st->lock);
        int running = st->running;
        if (!running) {
            pthread_mutex_unlock(&st->lock);
            break;
        }
        st->tick++;
        int tick = st->tick;
        int fd = st->fd;
        pthread_mutex_unlock(&st->lock);

        msg_resp_t hdr = {.resp = RESP_TICK};
        msg_tick_t t = {.tick = tick};

        if (ipc_send_all(fd, &hdr, sizeof(hdr)) != 0) break;
        if (ipc_send_all(fd, &t, sizeof(t)) != 0) break;

        printf("[server] tick=%d\n", tick);
        fflush(stdout);
    }

    return NULL;
}

static void *comm_thread(void *arg) {
    server_state_t *st = (server_state_t *)arg;

    for (;;) {
        msg_cmd_t cmd;
        if (ipc_recv_all(st->fd, &cmd, sizeof(cmd)) != 0) {
            printf("[server] client disconnected\n");
            pthread_mutex_lock(&st->lock);
            st->running = 0;
            pthread_mutex_unlock(&st->lock);
            break;
        }

        if (cmd.cmd == CMD_PING) {
            msg_resp_t resp = {.resp = RESP_PONG};
            (void)ipc_send_all(st->fd, &resp, sizeof(resp));
            printf("[server] PING -> PONG\n");
        } else if (cmd.cmd == CMD_QUIT) {
            msg_resp_t resp = {.resp = RESP_BYE};
            (void)ipc_send_all(st->fd, &resp, sizeof(resp));
            printf("[server] QUIT -> BYE\n");

            pthread_mutex_lock(&st->lock);
            st->running = 0;
            pthread_mutex_unlock(&st->lock);
            break;
        } else {
            printf("[server] unknown cmd=%d\n", cmd.cmd);
        }
        fflush(stdout);
    }

    return NULL;
}

int main(void) {
    int listen_fd = ipc_server_listen(SNAKE_SOCK_PATH);
    printf("[server] Listening on %s\n", SNAKE_SOCK_PATH);

    int fd = ipc_server_accept(listen_fd);
    printf("[server] Client connected\n");

    server_state_t st;
    st.fd = fd;
    st.running = 1;
    st.tick = 0;
    pthread_mutex_init(&st.lock, NULL);

    pthread_t th_game, th_comm;
    if (pthread_create(&th_game, NULL, game_loop_thread, &st) != 0) {
        perror("pthread_create(game)");
        return 1;
    }
    if (pthread_create(&th_comm, NULL, comm_thread, &st) != 0) {
        perror("pthread_create(comm)");
        return 1;
    }

    pthread_join(th_comm, NULL);

    pthread_mutex_lock(&st.lock);
    st.running = 0;
    pthread_mutex_unlock(&st.lock);

    pthread_join(th_game, NULL);

    pthread_mutex_destroy(&st.lock);
    close(fd);
    close(listen_fd);
    unlink(SNAKE_SOCK_PATH);

    printf("[server] shutdown complete\n");
    return 0;
}
