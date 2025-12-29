#include "ipc.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

static void die(const char *msg) {
    perror(msg);
    exit(1);
}

int ipc_send_all(int fd, const void *buf, size_t n) {
    const char *p = (const char *)buf;
    size_t sent = 0;

    while (sent < n) {
        ssize_t s = send(fd, p + sent, n - sent, 0);
        if (s < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (s == 0) return -1;
        sent += (size_t)s;
    }
    return 0;
}

int ipc_recv_all(int fd, void *buf, size_t n) {
    char *p = (char *)buf;
    size_t recvd = 0;

    while (recvd < n) {
        ssize_t r = recv(fd, p + recvd, n - recvd, 0);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) return -1; // peer closed
        recvd += (size_t)r;
    }
    return 0;
}

int ipc_server_listen(const char *path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) die("socket");

    // Remove old socket file if exists
    unlink(path);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) die("bind");
    if (listen(fd, 1) < 0) die("listen");

    return fd;
}

int ipc_server_accept(int listen_fd) {
    int cfd = accept(listen_fd, NULL, NULL);
    if (cfd < 0) die("accept");
    return cfd;
}

int ipc_client_connect(const char *path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) die("socket");

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) die("connect");
    return fd;
}

