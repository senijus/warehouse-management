#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "server.h"
#include "http.h"
#include "handler.h"

#define MAX_EVENTS  64
#define BUF_SIZE    16384
#define READ_TIMEOUT 5

static int listen_fd = -1;
static int epoll_fd  = -1;
static int running   = 0;

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* Wait for data with timeout using select */
static int wait_for_data(int fd, int timeout_sec) {
    fd_set rfds;
    struct timeval tv;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    tv.tv_sec  = timeout_sec;
    tv.tv_usec = 0;
    return select(fd + 1, &rfds, NULL, NULL, &tv);
}

static void handle_client(int client_fd) {
    char buf[BUF_SIZE];
    int total_read = 0;

    /* Read full request with select-based timeout */
    while (total_read < BUF_SIZE - 1) {
        int ready = wait_for_data(client_fd, READ_TIMEOUT);
        if (ready <= 0) break;

        int n = read(client_fd, buf + total_read, BUF_SIZE - total_read - 1);
        if (n <= 0) break;

        total_read += n;
        buf[total_read] = '\0';

        /* Check if headers are complete */
        char *header_end = strstr(buf, "\r\n\r\n");
        if (header_end) {
            /* Parse request to get Content-Length */
            HttpRequest tmp_req;
            http_parse_request(buf, total_read, &tmp_req);

            int body_start = (int)(header_end + 4 - buf);
            int body_received = total_read - body_start;

            if (tmp_req.content_length > 0) {
                /* POST with body: wait for full body */
                if (body_received >= tmp_req.content_length) break;
                /* else continue reading */
            } else {
                /* No body (GET or POST without Content-Length) */
                break;
            }
        }
    }

    if (total_read > 0) {
        buf[total_read] = '\0';
        HttpRequest req;
        if (http_parse_request(buf, total_read, &req) == 0) {
            handle_request(&req, client_fd);
        } else {
            http_send_error(client_fd, 400, "Bad Request");
        }
    }

    close(client_fd);
}

int server_init(int port) {
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return -1;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(port);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listen_fd);
        return -1;
    }

    if (listen(listen_fd, 128) < 0) {
        perror("listen");
        close(listen_fd);
        return -1;
    }

    set_nonblocking(listen_fd);

    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("epoll_create1");
        close(listen_fd);
        return -1;
    }

    struct epoll_event ev;
    ev.events  = EPOLLIN;
    ev.data.fd = listen_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev);

    printf("Server listening on port %d\n", port);
    return 0;
}

void server_run(void) {
    struct epoll_event events[MAX_EVENTS];
    running = 1;

    while (running) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (nfds < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == listen_fd) {
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
                if (client_fd < 0) {
                    if (errno != EAGAIN && errno != EWOULDBLOCK)
                        perror("accept");
                    continue;
                }
                /* Keep client socket in blocking mode for reliable read/write */
                handle_client(client_fd);
            }
        }
    }
}

void server_stop(void) {
    running = 0;
    if (epoll_fd >= 0) close(epoll_fd);
    if (listen_fd >= 0) close(listen_fd);
}
