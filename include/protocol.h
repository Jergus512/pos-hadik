#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

typedef enum {
    CMD_PING = 1,
    CMD_QUIT = 2
} command_t;

typedef enum {
    RESP_PONG = 100,
    RESP_BYE  = 101,
    RESP_TICK = 102
} response_t;

typedef struct {
    int32_t cmd;   // command_t
} msg_cmd_t;

typedef struct {
    int32_t resp;  // response_t
} msg_resp_t;

typedef struct {
    int32_t tick;  // monotonically increasing tick counter
} msg_tick_t;

#endif // PROTOCOL_H
