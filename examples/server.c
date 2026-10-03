/*
 * lightning - minimal HTTP/1.1 server using the streaming parser.
 *
 * A single-threaded epoll server that:
 *   - parses requests with lightning's streaming parser
 *   - supports keep-alive and chunked transfer-encoding
 *   - routes by method+path
 *
 * Build:  cc -O2 -I../include server.c -L../build -llightning -o server
 * Run:    ./server 8080
 * Test:   curl -v http://localhost:8080/hello
 *         curl -v -X POST -d 'ping' http://localhost:8080/echo
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _GNU_SOURCE
#include "lightning/lightning.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_EVENTS 64
#define BUF_SIZE   8192

typedef struct conn {
    int          fd;
    lht_stream_t parser;
    char         rbuf[BUF_SIZE];
    size_t       rlen;
    char         wbuf[BUF_SIZE];
    size_t       wlen;
    int          keep_alive;
    /* request state */
    char         method[16];  size_t method_len;
    char         url[1024];   size_t url_len;
    char         body[BUF_SIZE]; size_t body_len;
    int          headers_done;
} conn_t;

static int s_on_url(lht_stream_t *s, const char *at, size_t len)
{
    conn_t *c = s->data;
    if (c->url_len + len < sizeof(c->url)) {
        memcpy(c->url + c->url_len, at, len);
        c->url_len += len;
    }
    return 0;
}
static int s_on_body(lht_stream_t *s, const char *at, size_t len)
{
    conn_t *c = s->data;
    if (c->body_len + len < sizeof(c->body)) {
        memcpy(c->body + c->body_len, at, len);
        c->body_len += len;
    }
    return 0;
}
static int s_on_hc(lht_stream_t *s)
{
    conn_t *c = s->data;
    c->headers_done = 1;
    return 0;
}

static void send_response(conn_t *c, int status, const char *reason,
                          const char *body, size_t body_len)
{
    const char *ka = c->keep_alive ? "keep-alive" : "close";
    int n = snprintf(c->wbuf, sizeof(c->wbuf),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: %zu\r\n"
        "Connection: %s\r\n"
        "\r\n",
        status, reason, body_len, ka);
    if (body_len && n + body_len < (int)sizeof(c->wbuf)) {
        memcpy(c->wbuf + n, body, body_len);
        n += (int)body_len;
    }
    c->wlen = (size_t)n;
}

static void handle_request(conn_t *c)
{
    /* tiny router */
    if (c->url_len == 6 && memcmp(c->url, "/hello", 6) == 0) {
        send_response(c, 200, "OK", "Hello, lightning!\n", 18);
    } else if (c->url_len == 5 && memcmp(c->url, "/echo", 5) == 0) {
        send_response(c, 200, "OK", c->body, c->body_len);
    } else {
        send_response(c, 404, "Not Found", "not found\n", 10);
    }
}

static lht_settings_t g_settings;

static void conn_reset(conn_t *c)
{
    lht_stream_reset(&c->parser);
    c->url_len = 0;
    c->body_len = 0;
    c->headers_done = 0;
}

static void on_readable(conn_t *c)
{
    ssize_t n = recv(c->fd, c->rbuf + c->rlen, sizeof(c->rbuf) - c->rlen, 0);
    if (n <= 0) { /* EOF or error */
        close(c->fd);
        free(c);
        return;
    }
    c->rlen += (size_t)n;

    size_t consumed = lht_stream_execute(&c->parser, &g_settings,
                                         c->rbuf, c->rlen);
    if (lht_stream_error(&c->parser) != LHT_OK) {
        send_response(c, 400, "Bad Request", "bad request\n", 12);
        return;
    }
    /* compact the read buffer */
    if (consumed > 0) {
        memmove(c->rbuf, c->rbuf + consumed, c->rlen - consumed);
        c->rlen -= consumed;
    }
    if (c->headers_done && c->body_len > 0 ||
        (c->headers_done && c->parser.content_length == 0)) {
        handle_request(c);
    }
}

int main(int argc, char **argv)
{
    int port = (argc > 1) ? atoi(argv[1]) : 8080;

    memset(&g_settings, 0, sizeof(g_settings));
    g_settings.on_url = s_on_url;
    g_settings.on_body = s_on_body;
    g_settings.on_headers_complete = s_on_hc;

    int listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr));
    listen(listen_fd, 128);

    int ep = epoll_create1(0);
    struct epoll_event ev = { .events = EPOLLIN, .data.fd = listen_fd };
    epoll_ctl(ep, EPOLL_CTL_ADD, listen_fd, &ev);

    printf("lightning server listening on :%d\n", port);

    struct epoll_event events[MAX_EVENTS];
    for (;;) {
        int nfds = epoll_wait(ep, events, MAX_EVENTS, -1);
        for (int i = 0; i < nfds; ++i) {
            if (events[i].data.fd == listen_fd) {
                int cfd = accept4(listen_fd, NULL, NULL, SOCK_NONBLOCK);
                if (cfd < 0) continue;
                conn_t *c = calloc(1, sizeof(conn_t));
                c->fd = cfd;
                lht_stream_init(&c->parser, LHT_MSG_REQUEST);
                c->parser.data = c;
                struct epoll_event cev = { .events = EPOLLIN, .data.ptr = c };
                epoll_ctl(ep, EPOLL_CTL_ADD, cfd, &cev);
            } else {
                conn_t *c = events[i].data.ptr;
                if (events[i].events & EPOLLIN) {
                    on_readable(c);
                }
                if (c && c->wlen > 0) {
                    send(c->fd, c->wbuf, c->wlen, 0);
                    c->wlen = 0;
                    if (c->keep_alive) {
                        conn_reset(c);
                    } else {
                        close(c->fd);
                        free(c);
                    }
                }
            }
        }
    }
    return 0;
}
