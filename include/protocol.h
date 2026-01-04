#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

typedef enum {
    CMD_PING = 1,
    CMD_QUIT = 2,
    CMD_DIR  = 3,
    CMD_TOGGLE_PAUSE = 4,
    CMD_RESTART = 5,
    CMD_SET_WORLD = 6,     // arg: world_type_t
    CMD_SET_SIZE  = 7,     // arg: (w << 16) | (h & 0xFFFF)
    CMD_SET_MODE  = 8,     // arg: game_mode_t
    CMD_SET_TIME  = 9,     // arg: seconds
    CMD_BACK_TO_MENU = 10  // end session, return to menu
} command_t;

typedef enum {
    RESP_PONG     = 100,
    RESP_BYE      = 101,
    RESP_SNAPSHOT = 200
} response_t;

typedef enum {
    DIR_UP = 1,
    DIR_DOWN = 2,
    DIR_LEFT = 3,
    DIR_RIGHT = 4
} dir_t;

typedef enum {
    WORLD_WRAP = 1,
    WORLD_OBSTACLES = 2
} world_type_t;

typedef enum {
    MODE_STANDARD = 1,
    MODE_TIMED = 2
} game_mode_t;

typedef struct {
    int32_t cmd;
    int32_t arg;
} msg_cmd_t;

typedef struct {
    int32_t resp;
} msg_resp_t;

typedef struct {
    int32_t w, h;
    int32_t score;
    int32_t paused;
    int32_t gameover;
    int32_t fruit_x, fruit_y;

    int32_t snake_len;

    int32_t mode;        // MODE_*
    int32_t elapsed_s;
    int32_t time_left_s; // -1 for standard
} msg_snapshot_t;

typedef struct {
    int16_t x;
    int16_t y;
} msg_point_t;

#endif
