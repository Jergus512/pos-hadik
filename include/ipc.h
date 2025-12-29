#ifndef IPC_H
#define IPC_H

#include <stddef.h>

#define SNAKE_SOCK_PATH "/tmp/pos_snake.sock"

int ipc_server_listen(const char *path);              // returns listening fd
int ipc_server_accept(int listen_fd);                 // returns connected fd
int ipc_client_connect(const char *path);             // returns connected fd

int ipc_send_all(int fd, const void *buf, size_t n);  // 0 ok, -1 error
int ipc_recv_all(int fd, void *buf, size_t n);        // 0 ok, -1 error

#endif // IPC_H

