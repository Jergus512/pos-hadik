#include "ipc.h"
#include "protocol.h"

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <ncurses.h>

typedef struct {
    int fd;
    volatile sig_atomic_t running;

    pthread_mutex_t lock;
    msg_snapshot_t last;
    int have_last;
} client_state_t;

static void cleanup_curses(void) {
    endwin();
}

static void handle_signal(int sig) {
    (void)sig;
    cleanup_curses();
    _exit(130);
}

static void send_dir(int fd, dir_t d) {
    msg_cmd_t m;
    m.cmd = CMD_DIR;
    m.arg = (int32_t)d;
    (void)ipc_send_all(fd, &m, sizeof(m));
}

static void send_quit(int fd) {
    msg_cmd_t q;
    q.cmd = CMD_QUIT;
    q.arg = 0;
    (void)ipc_send_all(fd, &q, sizeof(q));
}

static void *recv_thread(void *arg) {
    client_state_t *st = (client_state_t *)arg;

    while (st->running) {
        msg_resp_t hdr;
        if (ipc_recv_all(st->fd, &hdr, sizeof(hdr)) != 0) break;

        if (hdr.resp == RESP_BYE) {
            break;
        } else if (hdr.resp == RESP_SNAPSHOT) {
            msg_snapshot_t snap;
            if (ipc_recv_all(st->fd, &snap, sizeof(snap)) != 0) break;

            pthread_mutex_lock(&st->lock);
            st->last = snap;
            st->have_last = 1;
            pthread_mutex_unlock(&st->lock);
        } else if (hdr.resp == RESP_PONG) {
            // ignore
        } else {
            // ignore unknown
        }
    }

    st->running = 0;
    return NULL;
}

static void render_snapshot(const msg_snapshot_t *s) {
    erase();

    mvprintw(0, 0, "SCORE: %d | Controls: W A S D (no Enter), Q quit", s->score);

    int top = 2;
    for (int y = 0; y < s->h; y++) {
        for (int x = 0; x < s->w; x++) {
            char c = '.';
            if (x == s->fruit_x && y == s->fruit_y) c = 'F';
            if (x == s->snake_x && y == s->snake_y) c = 'S';
            mvaddch(top + y, x, c);
        }
    }

    refresh();
}

int main(void) {
    int fd = ipc_client_connect(SNAKE_SOCK_PATH);

    // ncurses init: immediate key input, stable screen rendering
    initscr();
    cbreak();              // no Enter required
    noecho();              // do not echo keys
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE); // getch() non-blocking
    curs_set(0);           // hide cursor

    atexit(cleanup_curses);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    client_state_t st;
    memset(&st, 0, sizeof(st));
    st.fd = fd;
    st.running = 1;
    st.have_last = 0;
    pthread_mutex_init(&st.lock, NULL);

    pthread_t th_recv;
    if (pthread_create(&th_recv, NULL, recv_thread, &st) != 0) {
        endwin();
        perror("pthread_create(recv)");
        close(fd);
        return 1;
    }

    while (st.running) {
        int ch = getch(); // non-blocking
        if (ch == 'w' || ch == 'W') send_dir(fd, DIR_UP);
        else if (ch == 's' || ch == 'S') send_dir(fd, DIR_DOWN);
        else if (ch == 'a' || ch == 'A') send_dir(fd, DIR_LEFT);
        else if (ch == 'd' || ch == 'D') send_dir(fd, DIR_RIGHT);
        else if (ch == 'q' || ch == 'Q') {
            send_quit(fd);
            st.running = 0;
            break;
        }

        pthread_mutex_lock(&st.lock);
        int have = st.have_last;
        msg_snapshot_t snap = st.last;
        pthread_mutex_unlock(&st.lock);

        if (have) {
            render_snapshot(&snap);
        }

        usleep(20000); // ~50 FPS cap
    }

    pthread_join(th_recv, NULL);

    pthread_mutex_destroy(&st.lock);
    close(fd);

    endwin();
    printf("[client] exit\n");
    return 0;
}
