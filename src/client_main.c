#include "ipc.h"
#include "protocol.h"

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <ncurses.h>

typedef struct {
    int fd;
    volatile sig_atomic_t running;

    pthread_mutex_t lock;
    msg_snapshot_t last;
    int have_last;

    time_t start_time;
} client_state_t;

static void cleanup_curses(void) {
    endwin();
}

static void handle_signal(int sig) {
    (void)sig;
    cleanup_curses();
    _exit(130);
}

enum {
    CP_BORDER = 1,
    CP_SNAKE  = 2,
    CP_FRUIT  = 3,
    CP_TEXT   = 4
};

static void init_curses(void) {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    curs_set(0);

    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(CP_BORDER, COLOR_CYAN, -1);
        init_pair(CP_SNAKE,  COLOR_GREEN, -1);
        init_pair(CP_FRUIT,  COLOR_RED, -1);
        init_pair(CP_TEXT,   COLOR_WHITE, -1);
    }

    atexit(cleanup_curses);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
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
        }
    }

    st->running = 0;
    return NULL;
}

static void draw_border(int top, int left, int w, int h) {
    if (has_colors()) attron(COLOR_PAIR(CP_BORDER));

    mvaddch(top, left, ACS_ULCORNER);
    mvaddch(top, left + w + 1, ACS_URCORNER);
    mvaddch(top + h + 1, left, ACS_LLCORNER);
    mvaddch(top + h + 1, left + w + 1, ACS_LRCORNER);

    for (int x = 0; x < w; x++) {
        mvaddch(top, left + 1 + x, ACS_HLINE);
        mvaddch(top + h + 1, left + 1 + x, ACS_HLINE);
    }
    for (int y = 0; y < h; y++) {
        mvaddch(top + 1 + y, left, ACS_VLINE);
        mvaddch(top + 1 + y, left + w + 1, ACS_VLINE);
    }

    if (has_colors()) attroff(COLOR_PAIR(CP_BORDER));
}

static void render_snapshot(const client_state_t *st, const msg_snapshot_t *s) {
    erase();

    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    int top = 2;
    int left = 2;

    if (has_colors()) attron(COLOR_PAIR(CP_TEXT));
    mvprintw(0, 2, "POS Snake | WASD move | Q quit");
    int elapsed = (int)difftime(time(NULL), st->start_time);
    mvprintw(1, 2, "Score: %d   Time: %ds   Map: %dx%d", s->score, elapsed, s->w, s->h);
    if (has_colors()) attroff(COLOR_PAIR(CP_TEXT));

    if (rows < top + s->h + 3 || cols < left + s->w + 3) {
        if (has_colors()) attron(COLOR_PAIR(CP_TEXT));
        mvprintw(3, 2, "Terminal too small. Resize window.");
        if (has_colors()) attroff(COLOR_PAIR(CP_TEXT));
        refresh();
        return;
    }

    draw_border(top, left, s->w, s->h);

    // fruit
    if (has_colors()) attron(COLOR_PAIR(CP_FRUIT));
    mvaddch(top + 1 + s->fruit_y, left + 1 + s->fruit_x, 'o');
    if (has_colors()) attroff(COLOR_PAIR(CP_FRUIT));

    // snake head
    if (has_colors()) attron(COLOR_PAIR(CP_SNAKE));
    mvaddch(top + 1 + s->snake_y, left + 1 + s->snake_x, '@');
    if (has_colors()) attroff(COLOR_PAIR(CP_SNAKE));

    refresh();
}

int main(void) {
    int fd = ipc_client_connect(SNAKE_SOCK_PATH);

    init_curses();

    client_state_t st;
    memset(&st, 0, sizeof(st));
    st.fd = fd;
    st.running = 1;
    st.have_last = 0;
    st.start_time = time(NULL);
    pthread_mutex_init(&st.lock, NULL);

    pthread_t th_recv;
    if (pthread_create(&th_recv, NULL, recv_thread, &st) != 0) {
        endwin();
        perror("pthread_create(recv)");
        close(fd);
        return 1;
    }

    while (st.running) {
        int ch = getch();
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
            render_snapshot(&st, &snap);
        }

        usleep(20000);
    }

    pthread_join(th_recv, NULL);
    pthread_mutex_destroy(&st.lock);
    close(fd);

    endwin();
    printf("[client] exit\n");
    return 0;
}
