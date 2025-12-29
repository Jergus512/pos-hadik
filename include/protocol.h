#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

typedef enum {
    CMD_PING = 1,
    CMD_QUIT = 2
} command_t;

typedef enum {
    RESP_PONG = 100,
    RESP_BYE  = 101
} response_t;

typedef struct {
    int32_t cmd;   // command_t
} msg_cmd_t;

typedef struct {
    int32_t resp;  // response_t
} msg_resp_t;

#endif // PROTOCOL_H

