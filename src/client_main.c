#define _DEFAULT_SOURCE

#include "ipc.h"
#include "protocol.h"

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <ncurses.h>
#include <stdint.h>
#include <errno.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_POINTS 4096
#define OB_W 45
#define OB_H 30
#define OB_FILE "assets/obstacles_45x30.txt"

typedef struct {
    int fd;
    volatile sig_atomic_t running;

    pthread_mutex_t lock;
    msg_snapshot_t snap;
    msg_point_t pts[MAX_POINTS];
    int have_last;

    int best_score;

    world_type_t world_type;
    int w, h;
    uint8_t *obst;
} client_state_t;

static void cleanup_curses(void) { endwin(); }

static void handle_signal(int sig) {
    (void)sig;
    cleanup_curses();
    _exit(130);
}

enum {
    CP_BORDER = 1,
    CP_SNAKE_HEAD  = 2,
    CP_SNAKE_BODY  = 3,
    CP_FRUIT  = 4,
    CP_TEXT   = 5,
    CP_OBST   = 6
};

static void term_size(int *cols, int *rows) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        *cols = (int)ws.ws_col;
        *rows = (int)ws.ws_row;
        return;
    }
    *cols = 120;
    *rows = 40;
}

static int load_best_score(void) {
    FILE *f = fopen("assets/highscore.txt", "r");
    if (!f) return 0;
    int v = 0;
    (void)fscanf(f, "%d", &v);
    fclose(f);
    if (v < 0) v = 0;
    return v;
}

static void save_best_score(int v) {
    FILE *f = fopen("assets/highscore.txt", "w");
    if (!f) return;
    fprintf(f, "%d\n", v);
    fclose(f);
}

static int load_obstacles_client(client_state_t *st, const char *path) {
    if (st->w <= 0 || st->h <= 0) return -1;
    size_t n = (size_t)st->w * (size_t)st->h;
    st->obst = (uint8_t *)calloc(n, 1);
    if (!st->obst) return -1;

    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[256];
    for (int y = 0; y < st->h; y++) {
        if (!fgets(line, (int)sizeof(line), f)) { fclose(f); return -1; }
        for (int x = 0; x < st->w; x++) st->obst[y * st->w + x] = (line[x] == '#') ? 1 : 0;
    }
    fclose(f);
    return 0;
}

static int obst_at_client(const client_state_t *st, int x, int y) {
    if (!st->obst) return 0;
    if (x < 0 || x >= st->w || y < 0 || y >= st->h) return 1;
    return st->obst[y * st->w + x] ? 1 : 0;
}

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
        init_pair(CP_SNAKE_HEAD, COLOR_GREEN, -1);
        init_pair(CP_SNAKE_BODY, COLOR_GREEN, -1);
        init_pair(CP_FRUIT, COLOR_RED, -1);
        init_pair(CP_TEXT, COLOR_WHITE, -1);
        init_pair(CP_OBST, COLOR_YELLOW, -1);
    }

    atexit(cleanup_curses);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
}

static void send_cmd(int fd, command_t c, int32_t arg) {
    msg_cmd_t m;
    m.cmd = (int32_t)c;
    m.arg = arg;
    (void)ipc_send_all(fd, &m, sizeof(m));
}

static void *recv_thread(void *arg) {
    client_state_t *st = (client_state_t *)arg;

    while (st->running) {
        msg_resp_t hdr;
        if (ipc_recv_all(st->fd, &hdr, sizeof(hdr)) != 0) break;

        if (hdr.resp == RESP_BYE) break;

        if (hdr.resp == RESP_SNAPSHOT) {
            msg_snapshot_t s;
            if (ipc_recv_all(st->fd, &s, sizeof(s)) != 0) break;

            int n = s.snake_len;
            if (n < 0) n = 0;
            if (n > MAX_POINTS) n = MAX_POINTS;

            msg_point_t tmp[MAX_POINTS];
            for (int i = 0; i < n; i++) {
                if (ipc_recv_all(st->fd, &tmp[i], sizeof(tmp[i])) != 0) {
                    st->running = 0;
                    return NULL;
                }
            }

            pthread_mutex_lock(&st->lock);
            st->snap = s;
            for (int i = 0; i < n; i++) st->pts[i] = tmp[i];
            st->have_last = 1;
            pthread_mutex_unlock(&st->lock);
        }
    }

    st->running = 0;
    return NULL;
}

static void draw_border(int top, int left, int w, int h) {
    (void)w; (void)h;
    if (has_colors()) attron(COLOR_PAIR(CP_BORDER));

    mvaddch(top, left, '+');
    mvaddch(top, left + w + 1, '+');
    mvaddch(top + h + 1, left, '+');
    mvaddch(top + h + 1, left + w + 1, '+');

    for (int x = 0; x < w; x++) {
        mvaddch(top, left + 1 + x, '-');
        mvaddch(top + h + 1, left + 1 + x, '-');
    }
    for (int y = 0; y < h; y++) {
        mvaddch(top + 1 + y, left, '|');
        mvaddch(top + 1 + y, left + w + 1, '|');
    }

    if (has_colors()) attroff(COLOR_PAIR(CP_BORDER));
}

static void center_text(int row, const char *txt) {
    int cols = getmaxx(stdscr);
    int len = (int)strlen(txt);
    int col = (cols - len) / 2;
    if (col < 0) col = 0;
    mvprintw(row, col, "%s", txt);
}

static void render_frame(const client_state_t *st, const msg_snapshot_t *s, const msg_point_t *pts) {
    erase();

    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    int top = 2;
    int left = 2;

    if (has_colors()) attron(COLOR_PAIR(CP_TEXT));
    mvprintw(0, 2, "POS Snake | WASD move | P pause | R restart | M menu | Q quit");
    if (s->mode == MODE_TIMED) {
        mvprintw(1, 2, "Score: %d  Best: %d  Elapsed: %ds  Left: %ds  Map: %dx%d",
                 s->score, st->best_score, s->elapsed_s, s->time_left_s, s->w, s->h);
    } else {
        mvprintw(1, 2, "Score: %d  Best: %d  Elapsed: %ds  Map: %dx%d",
                 s->score, st->best_score, s->elapsed_s, s->w, s->h);
    }
    if (has_colors()) attroff(COLOR_PAIR(CP_TEXT));

    if (rows < top + s->h + 3 || cols < left + s->w + 3) {
        if (has_colors()) attron(COLOR_PAIR(CP_TEXT));
        mvprintw(3, 2, "Terminal too small. Resize window (need at least %dx%d).",
                 left + s->w + 3, top + s->h + 3);
        if (has_colors()) attroff(COLOR_PAIR(CP_TEXT));
        refresh();
        return;
    }

    draw_border(top, left, s->w, s->h);

    if (st->world_type == WORLD_OBSTACLES && st->obst) {
        if (has_colors()) attron(COLOR_PAIR(CP_OBST));
        for (int y = 0; y < st->h; y++)
            for (int x = 0; x < st->w; x++)
                if (obst_at_client(st, x, y)) mvaddch(top + 1 + y, left + 1 + x, '#');
        if (has_colors()) attroff(COLOR_PAIR(CP_OBST));
    }

    if (has_colors()) attron(COLOR_PAIR(CP_FRUIT));
    mvaddch(top + 1 + s->fruit_y, left + 1 + s->fruit_x, 'o');
    if (has_colors()) attroff(COLOR_PAIR(CP_FRUIT));

    int n = s->snake_len;
    if (n > MAX_POINTS) n = MAX_POINTS;
    if (n > 0) {
        if (has_colors()) attron(COLOR_PAIR(CP_SNAKE_HEAD));
        mvaddch(top + 1 + pts[0].y, left + 1 + pts[0].x, '@');
        if (has_colors()) attroff(COLOR_PAIR(CP_SNAKE_HEAD));

        if (has_colors()) attron(COLOR_PAIR(CP_SNAKE_BODY));
        for (int i = 1; i < n; i++) {
            int x = pts[i].x;
            int y = pts[i].y;
            if (x >= 0 && x < s->w && y >= 0 && y < s->h) {
                mvaddch(top + 1 + y, left + 1 + x, 'o');
            }
        }
        if (has_colors()) attroff(COLOR_PAIR(CP_SNAKE_BODY));
    }

    if (s->paused) {
        if (has_colors()) attron(COLOR_PAIR(CP_TEXT));
        center_text(top + s->h / 2, "PAUSED");
        if (has_colors()) attroff(COLOR_PAIR(CP_TEXT));
    }

    if (s->gameover) {
        if (has_colors()) attron(COLOR_PAIR(CP_TEXT));
        center_text(top + (s->h / 2) - 1, "GAME OVER");
        center_text(top + (s->h / 2) + 1, "Press R to restart or M for menu");
        if (has_colors()) attroff(COLOR_PAIR(CP_TEXT));
    }

    refresh();
}

static int read_int_range(const char *prompt, int min, int max) {
    int v = 0;
    for (;;) {
        printf("%s ", prompt);
        fflush(stdout);
        if (scanf("%d", &v) != 1) {
            int c;
            while ((c = getchar()) != '\n' && c != EOF) {}
            continue;
        }
        if (v >= min && v <= max) return v;
        printf("Value must be in range %d..%d\n", min, max);
    }
}

/* ===================== fork+exec server management ===================== */

static pid_t server_pid = -1;

static int start_server_process(void) {
    // ak ostal socket po páde, radšej ho odstráň
    unlink(SNAKE_SOCK_PATH);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        // child -> spustí server
        execl("./build/server", "server", NULL);
        perror("exec ./build/server");
        _exit(1);
    }

    // parent -> klient
    server_pid = pid;

    // počkáme na socket (max ~2s)
    for (int i = 0; i < 20; i++) {
        if (access(SNAKE_SOCK_PATH, F_OK) == 0) return 0;
        usleep(100000);
    }

    fprintf(stderr, "Server did not create socket in time: %s\n", SNAKE_SOCK_PATH);
    return -1;
}

static void stop_server_process(void) {
    if (server_pid > 0) {
        (void)waitpid(server_pid, NULL, 0);
        server_pid = -1;
    }
}

/* ======================================================================= */

static int run_one_game(void) {
    int cols, rows;
    term_size(&cols, &rows);

    int max_w_term = cols - 5;
    int max_h_term = rows - 5;

    if (max_w_term < 10) max_w_term = 10;
    if (max_h_term < 10) max_h_term = 10;

    printf("Detected terminal: %dx%d\n", cols, rows);

    printf("GAME MODE:\n  1) Standard\n  2) Timed\n");
    int mode_in = read_int_range("Select (1-2):", 1, 2);

    int duration = 60;
    if (mode_in == MODE_TIMED) duration = read_int_range("Set time in seconds (10-3600):", 10, 3600);

    printf("WORLD TYPE:\n  1) No obstacles (WRAP)\n  2) With obstacles (fixed 45x30 from file)\n");
    int wt_in = read_int_range("Select (1-2):", 1, 2);

    int w = 0, h = 0;

    if (wt_in == WORLD_OBSTACLES) {
        w = OB_W; h = OB_H;
        printf("Using obstacle map %s (%dx%d)\n", OB_FILE, w, h);
        if (w > max_w_term || h > max_h_term) {
            printf("WARNING: obstacle map needs at least %dx%d terminal. Your terminal is %dx%d.\n",
                   w + 5, h + 5, cols, rows);
        }
    } else {
        int wmax = (max_w_term < 60) ? max_w_term : 60;
        int hmax = (max_h_term < 40) ? max_h_term : 40;

        char pw[128], ph[128];
        snprintf(pw, sizeof(pw), "Map width (10-%d):", wmax);
        snprintf(ph, sizeof(ph), "Map height (10-%d):", hmax);

        w = read_int_range(pw, 10, wmax);
        h = read_int_range(ph, 10, hmax);
    }

    // ========== NOVÁ HRA -> klient spustí server ==========
    if (start_server_process() != 0) {
        fprintf(stderr, "Failed to start server.\n");
        return 2;
    }
    // ======================================================

    int fd = ipc_client_connect(SNAKE_SOCK_PATH);

    send_cmd(fd, CMD_SET_MODE, mode_in);
    if (mode_in == MODE_TIMED) send_cmd(fd, CMD_SET_TIME, duration);
    send_cmd(fd, CMD_SET_WORLD, wt_in);

    if (wt_in == WORLD_WRAP) {
        int32_t packed = (int32_t)((w << 16) | (h & 0xFFFF));
        send_cmd(fd, CMD_SET_SIZE, packed);
    }

    init_curses();

    client_state_t st;
    memset(&st, 0, sizeof(st));
    st.fd = fd;
    st.running = 1;
    pthread_mutex_init(&st.lock, NULL);
    st.best_score = load_best_score();
    st.world_type = (world_type_t)wt_in;
    st.w = w;
    st.h = h;

    if (st.world_type == WORLD_OBSTACLES) {
        if (load_obstacles_client(&st, OB_FILE) != 0) {
            endwin();
            fprintf(stderr, "Failed to load obstacle file: %s\n", OB_FILE);
            close(fd);
            stop_server_process();
            return 2;
        }
    }

    pthread_t th_recv;
    if (pthread_create(&th_recv, NULL, recv_thread, &st) != 0) {
        endwin();
        perror("pthread_create(recv)");
        close(fd);
        free(st.obst);
        stop_server_process();
        return 2;
    }

    int go_menu = 0;

    while (st.running) {
        int ch = getch();

        if (ch == 'w' || ch == 'W') send_cmd(fd, CMD_DIR, DIR_UP);
        else if (ch == 's' || ch == 'S') send_cmd(fd, CMD_DIR, DIR_DOWN);
        else if (ch == 'a' || ch == 'A') send_cmd(fd, CMD_DIR, DIR_LEFT);
        else if (ch == 'd' || ch == 'D') send_cmd(fd, CMD_DIR, DIR_RIGHT);
        else if (ch == 'p' || ch == 'P') send_cmd(fd, CMD_TOGGLE_PAUSE, 0);
        else if (ch == 'r' || ch == 'R') send_cmd(fd, CMD_RESTART, 0);
        else if (ch == 'm' || ch == 'M') {
            // aby server zanikol a ostal iba klient v menu:
            send_cmd(fd, CMD_QUIT, 0);
            go_menu = 1;
            st.running = 0;
            break;
        } else if (ch == 'q' || ch == 'Q') {
            send_cmd(fd, CMD_QUIT, 0);
            go_menu = 0;
            st.running = 0;
            break;
        }

        pthread_mutex_lock(&st.lock);
        int have = st.have_last;
        msg_snapshot_t snap = st.snap;
        int n = snap.snake_len;
        if (n > MAX_POINTS) n = MAX_POINTS;
        msg_point_t local[MAX_POINTS];
        for (int i = 0; i < n; i++) local[i] = st.pts[i];
        pthread_mutex_unlock(&st.lock);

        if (have) {
            if (snap.score > st.best_score) st.best_score = snap.score;
            render_frame(&st, &snap, local);
        }

        usleep(20000);
    }

    pthread_join(th_recv, NULL);

    save_best_score(st.best_score);

    free(st.obst);
    pthread_mutex_destroy(&st.lock);
    close(fd);

    endwin();

    // ========== po skončení hry server musí zaniknúť ==========
    stop_server_process();
    // =========================================================

    return go_menu ? 0 : 2;
}

int main(void) {
    for (;;) {
        int rc = run_one_game();
        if (rc == 0) {
            printf("\n[client] Returned to menu.\n\n");
            continue;
        }
        printf("[client] exit\n");
        return 0;
    }
}
