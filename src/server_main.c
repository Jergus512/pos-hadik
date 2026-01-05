#include "ipc.h"
#include "protocol.h"

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>

static void sleep_ms(long ms) {
    if (ms <= 0) return;
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {}
}

#include <string.h>
#include <stdint.h>

#define MIN_W 10
#define MIN_H 10
#define MAX_W 60
#define MAX_H 40

#define OB_W 45
#define OB_H 30
#define OB_FILE "assets/obstacles_45x30.txt"

#define MIN_TIME 10
#define MAX_TIME 3600

typedef struct {
    pthread_mutex_t lock;
    int running;

    int client_fd;        // -1 if no client
    int session_active;   // 1 if game session is active for current client

    int w, h;
    world_type_t world_type;

    game_mode_t mode;
    int duration_s;
    time_t game_start_ts;
    time_t pause_start_ts;
    int paused_total_s;

    msg_point_t *buf;
    int cap;
    int head_idx;
    int len;

    int fruit_x, fruit_y;
    int score;
    int paused;
    int gameover;

    dir_t dir;
    dir_t requested_dir;
    int grow_pending;

    uint8_t *obst; // w*h
} server_state_t;

static int rand_range(int a, int b) { return a + rand() % (b - a + 1); }

static int is_opposite(dir_t a, dir_t b) {
    return (a == DIR_UP && b == DIR_DOWN) ||
           (a == DIR_DOWN && b == DIR_UP) ||
           (a == DIR_LEFT && b == DIR_RIGHT) ||
           (a == DIR_RIGHT && b == DIR_LEFT);
}

static int obst_at(const server_state_t *st, int x, int y) {
    if (!st->obst) return 0;
    if (x < 0 || x >= st->w || y < 0 || y >= st->h) return 1;
    return st->obst[y * st->w + x] ? 1 : 0;
}

static msg_point_t snake_get(const server_state_t *st, int i) {
    return st->buf[(st->head_idx + i) % st->cap];
}

static void snake_set_head(server_state_t *st, msg_point_t p) {
    st->head_idx = (st->head_idx - 1 + st->cap) % st->cap;
    st->buf[st->head_idx] = p;
}

static int snake_contains(server_state_t *st, msg_point_t p, int allow_tail) {
    for (int i = 0; i < st->len; i++) {
        msg_point_t q = snake_get(st, i);
        if (q.x == p.x && q.y == p.y) {
            if (allow_tail && i == st->len - 1) return 0;
            return 1;
        }
    }
    return 0;
}

static void free_obstacles(server_state_t *st) {
    free(st->obst);
    st->obst = NULL;
}

static int load_obstacles(server_state_t *st, const char *path) {
    free_obstacles(st);

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

static void ensure_buffers(server_state_t *st) {
    int need = st->w * st->h;
    if (need <= 0) need = 1;
    if (st->cap != need) {
        free(st->buf);
        st->cap = need;
        st->buf = (msg_point_t *)calloc((size_t)st->cap, sizeof(msg_point_t));
        if (!st->buf) { perror("calloc"); exit(1); }
    }
}

static void spawn_fruit(server_state_t *st) {
    for (;;) {
        int x = rand_range(0, st->w - 1);
        int y = rand_range(0, st->h - 1);
        if (st->world_type == WORLD_OBSTACLES && obst_at(st, x, y)) continue;

        int ok = 1;
        for (int i = 0; i < st->len; i++) {
            msg_point_t q = snake_get(st, i);
            if (q.x == x && q.y == y) { ok = 0; break; }
        }
        if (ok) { st->fruit_x = x; st->fruit_y = y; return; }
    }
}

static void reset_game(server_state_t *st) {
    ensure_buffers(st);

    st->score = 0;
    st->paused = 0;
    st->pause_start_ts = 0;
    st->paused_total_s = 0;
    st->gameover = 0;
    st->grow_pending = 0;

    st->dir = DIR_RIGHT;
    st->requested_dir = DIR_RIGHT;

    st->len = 3;
    st->head_idx = 0;

    int cx = st->w / 2;
    int cy = st->h / 2;

    if (st->world_type == WORLD_OBSTACLES) {
        int tries = 0;
        while (tries < 5000 && (obst_at(st, cx, cy) || obst_at(st, cx - 1, cy) || obst_at(st, cx - 2, cy))) {
            cx = rand_range(2, st->w - 2);
            cy = rand_range(1, st->h - 2);
            tries++;
        }
    }

    st->buf[0] = (msg_point_t){(int16_t)cx, (int16_t)cy};
    st->buf[1] = (msg_point_t){(int16_t)(cx - 1), (int16_t)cy};
    st->buf[2] = (msg_point_t){(int16_t)(cx - 2), (int16_t)cy};

    st->game_start_ts = time(NULL);
    spawn_fruit(st);

    st->session_active = 1;
}

static int elapsed_s(const server_state_t *st) {
    time_t now = time(NULL);

    int extra_pause = 0;
    if (st->paused && st->pause_start_ts != 0) {
        extra_pause = (int)difftime(now, st->pause_start_ts);
        if (extra_pause < 0) extra_pause = 0;
    }

    int e = (int)difftime(now, st->game_start_ts) - st->paused_total_s - extra_pause;
    if (e < 0) e = 0;
    return e;
}
static int time_left_s(const server_state_t *st) {
    if (st->mode != MODE_TIMED) return -1;
    int left = st->duration_s - elapsed_s(st);
    if (left < 0) left = 0;
    return left;
}

static void send_snapshot_locked(server_state_t *st) {
    if (!st->session_active) return;
    if (st->client_fd < 0) return;

    msg_resp_t hdr = {RESP_SNAPSHOT};

    msg_snapshot_t s;
    s.w = st->w;
    s.h = st->h;
    s.score = st->score;
    s.paused = st->paused;
    s.gameover = st->gameover;
    s.fruit_x = st->fruit_x;
    s.fruit_y = st->fruit_y;
    s.snake_len = st->len;
    s.mode = st->mode;
    s.elapsed_s = elapsed_s(st);
    s.time_left_s = time_left_s(st);

    (void)ipc_send_all(st->client_fd, &hdr, sizeof(hdr));
    (void)ipc_send_all(st->client_fd, &s, sizeof(s));
    for (int i = 0; i < st->len; i++) {
        msg_point_t p = snake_get(st, i);
        (void)ipc_send_all(st->client_fd, &p, sizeof(p));
    }
}

static void tick_locked(server_state_t *st) {
    if (!st->session_active) return;

    dir_t nd = st->requested_dir;
    if (st->len > 1 && is_opposite(st->dir, nd)) nd = st->dir;
    st->dir = nd;

    msg_point_t h = snake_get(st, 0);
    int nx = h.x, ny = h.y;

    if (st->dir == DIR_UP) ny--;
    else if (st->dir == DIR_DOWN) ny++;
    else if (st->dir == DIR_LEFT) nx--;
    else if (st->dir == DIR_RIGHT) nx++;

    if (st->world_type == WORLD_WRAP) {
        if (nx < 0) nx = st->w - 1;
        else if (nx >= st->w) nx = 0;
        if (ny < 0) ny = st->h - 1;
        else if (ny >= st->h) ny = 0;
    } else {
        if (nx < 0 || nx >= st->w || ny < 0 || ny >= st->h) { st->gameover = 1; return; }
        if (obst_at(st, nx, ny)) { st->gameover = 1; return; }
    }

    msg_point_t nh = {(int16_t)nx, (int16_t)ny};

    if (snake_contains(st, nh, st->grow_pending == 0)) { st->gameover = 1; return; }

    snake_set_head(st, nh);

    if (st->grow_pending > 0) {
        st->len++;
        st->grow_pending--;
        if (st->len > st->cap) st->len = st->cap;
    }

    if (nx == st->fruit_x && ny == st->fruit_y) {
        st->score += 10;
        st->grow_pending++;
        spawn_fruit(st);
    }
}

static void *game_thread(void *arg) {
    server_state_t *st = (server_state_t *)arg;

    while (st->running) {
        sleep_ms(120);

        pthread_mutex_lock(&st->lock);

        if (st->session_active && !st->gameover && st->mode == MODE_TIMED) {
            if (time_left_s(st) <= 0) st->gameover = 1;
        }

        if (st->session_active && !st->paused && !st->gameover) tick_locked(st);

        send_snapshot_locked(st);

        pthread_mutex_unlock(&st->lock);
    }
    return NULL;
}

static int wait_config_and_start(server_state_t *st) {
    int got_mode = 0, got_time = 0, got_world = 0, got_size = 0;

    // defaults
    st->session_active = 0;
    st->paused = 0;
    st->pause_start_ts = 0;
    st->paused_total_s = 0;
    st->gameover = 0;

    st->mode = MODE_STANDARD;
    st->duration_s = 60;
    st->world_type = WORLD_WRAP;
    st->w = 20;
    st->h = 15;
    free_obstacles(st);

    while (!(got_mode && got_world && got_size && (st->mode != MODE_TIMED || got_time))) {
        msg_cmd_t cmd;
        if (ipc_recv_all(st->client_fd, &cmd, sizeof(cmd)) != 0) return -1;

        if (cmd.cmd == CMD_QUIT) return 1; // shutdown server

        if (cmd.cmd == CMD_SET_MODE) {
            if (cmd.arg == MODE_STANDARD || cmd.arg == MODE_TIMED) { st->mode = (game_mode_t)cmd.arg; got_mode = 1; }
        } else if (cmd.cmd == CMD_SET_TIME) {
            if (cmd.arg >= MIN_TIME && cmd.arg <= MAX_TIME) { st->duration_s = cmd.arg; got_time = 1; }
        } else if (cmd.cmd == CMD_SET_WORLD) {
            if (cmd.arg == WORLD_WRAP || cmd.arg == WORLD_OBSTACLES) {
                st->world_type = (world_type_t)cmd.arg;
                got_world = 1;
                if (st->world_type == WORLD_OBSTACLES) { st->w = OB_W; st->h = OB_H; got_size = 1; }
            }
        } else if (cmd.cmd == CMD_SET_SIZE) {
            if (st->world_type == WORLD_OBSTACLES) {
                got_size = 1;
            } else {
                int w = (cmd.arg >> 16) & 0xFFFF;
                int h = cmd.arg & 0xFFFF;
                if (w >= MIN_W && w <= MAX_W && h >= MIN_H && h <= MAX_H) { st->w = w; st->h = h; got_size = 1; }
            }
        }
    }

    ensure_buffers(st);

    if (st->world_type == WORLD_OBSTACLES) {
        if (load_obstacles(st, OB_FILE) != 0) {
            fprintf(stderr, "[server] failed to load obstacles file: %s\n", OB_FILE);
            return -1;
        }
    }

    reset_game(st);
    return 0;
}

int main(void) {
    srand((unsigned)time(NULL));

    int lfd = ipc_server_listen(SNAKE_SOCK_PATH);
    printf("[server] Listening on %s\n", SNAKE_SOCK_PATH);

    server_state_t st;
    memset(&st, 0, sizeof(st));
    st.running = 1;
    st.client_fd = -1;
    pthread_mutex_init(&st.lock, NULL);

    pthread_t th;
    if (pthread_create(&th, NULL, game_thread, &st) != 0) {
        perror("pthread_create");
        close(lfd);
        unlink(SNAKE_SOCK_PATH);
        return 1;
    }

    while (st.running) {
        int cfd = ipc_server_accept(lfd);
        printf("[server] Client connected\n");

        pthread_mutex_lock(&st.lock);
        st.client_fd = cfd;
        int cfg = wait_config_and_start(&st);
        pthread_mutex_unlock(&st.lock);

        if (cfg == 1) {
            msg_resp_t bye = {RESP_BYE};
            (void)ipc_send_all(cfd, &bye, sizeof(bye));
            close(cfd);
            st.client_fd = -1;
            st.running = 0;
            break;
        }
        if (cfg != 0) {
            close(cfd);
            pthread_mutex_lock(&st.lock);
            st.client_fd = -1;
            st.session_active = 0;
            pthread_mutex_unlock(&st.lock);
            continue;
        }

        // session loop
        for (;;) {
            msg_cmd_t cmd;
            if (ipc_recv_all(cfd, &cmd, sizeof(cmd)) != 0) {
                pthread_mutex_lock(&st.lock);
                st.session_active = 0;
                st.client_fd = -1;
                pthread_mutex_unlock(&st.lock);
                close(cfd);
                break;
            }

            pthread_mutex_lock(&st.lock);

            if (cmd.cmd == CMD_DIR) {
                st.requested_dir = (dir_t)cmd.arg;
                pthread_mutex_unlock(&st.lock);
                continue;
            }

            if (cmd.cmd == CMD_TOGGLE_PAUSE) {
                if (!st.gameover) {
                    if (!st.paused) {
                        st.paused = 1;
                        st.pause_start_ts = time(NULL);
                    } else {
                        st.paused = 0;
                        if (st.pause_start_ts != 0) {
                            int d = (int)difftime(time(NULL), st.pause_start_ts);
                            if (d > 0) st.paused_total_s += d;
                            st.pause_start_ts = 0;
                        }
                    }
                }
                pthread_mutex_unlock(&st.lock);
                continue;
            }

            if (cmd.cmd == CMD_RESTART) {
                reset_game(&st);
                pthread_mutex_unlock(&st.lock);
                continue;
            }

            if (cmd.cmd == CMD_BACK_TO_MENU) {
                msg_resp_t bye = {RESP_BYE};
                (void)ipc_send_all(cfd, &bye, sizeof(bye));
                st.session_active = 0;
                st.client_fd = -1;
                pthread_mutex_unlock(&st.lock);
                close(cfd);
                break;
            }

            if (cmd.cmd == CMD_QUIT) {
                msg_resp_t bye = {RESP_BYE};
                (void)ipc_send_all(cfd, &bye, sizeof(bye));
                st.session_active = 0;
                st.client_fd = -1;
                st.running = 0;
                pthread_mutex_unlock(&st.lock);
                close(cfd);
                break;
            }

            pthread_mutex_unlock(&st.lock);
        }

    }

    pthread_mutex_lock(&st.lock);
    st.running = 0;
    pthread_mutex_unlock(&st.lock);

    pthread_join(th, NULL);

    free(st.obst);
    free(st.buf);
    pthread_mutex_destroy(&st.lock);
    close(lfd);
    unlink(SNAKE_SOCK_PATH);
    printf("[server] shutdown\n");
    return 0;
}
