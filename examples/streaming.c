/*
 * lightning - streaming parser example: feed data in arbitrary chunks.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "lightning/lightning.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    char url[256];   size_t url_len;
    char body[4096]; size_t body_len;
    int  headers_done;
    int  msg_done;
} ctx_t;

static int on_url(lht_stream_t *s, const char *at, size_t len)
{
    ctx_t *c = s->data;
    if (c->url_len + len < sizeof(c->url)) {
        memcpy(c->url + c->url_len, at, len);
        c->url_len += len;
    }
    return 0;
}
static int on_body(lht_stream_t *s, const char *at, size_t len)
{
    ctx_t *c = s->data;
    if (c->body_len + len < sizeof(c->body)) {
        memcpy(c->body + c->body_len, at, len);
        c->body_len += len;
    }
    return 0;
}
static int on_hc(lht_stream_t *s)  { ((ctx_t*)s->data)->headers_done = 1; return 0; }
static int on_mc(lht_stream_t *s)  { ((ctx_t*)s->data)->msg_done = 1; return 0; }

int main(void)
{
    const char *full =
        "POST /upload HTTP/1.1\r\n"
        "Content-Length: 11\r\n"
        "\r\n"
        "hello world";

    lht_settings_t settings;
    memset(&settings, 0, sizeof(settings));
    settings.on_url = on_url;
    settings.on_body = on_body;
    settings.on_headers_complete = on_hc;
    settings.on_message_complete = on_mc;

    lht_stream_t parser;
    lht_stream_init(&parser, LHT_MSG_REQUEST);
    ctx_t ctx; memset(&ctx, 0, sizeof(ctx));
    parser.data = &ctx;

    /* simulate a slow socket: feed 4 bytes at a time */
    size_t total = 0;
    size_t len = strlen(full);
    while (total < len) {
        size_t chunk = (len - total < 4) ? (len - total) : 4;
        size_t n = lht_stream_execute(&parser, &settings, full + total, chunk);
        total += n;
        if (n == 0) break;
    }

    printf("url:    %.*s\n", (int)ctx.url_len, ctx.url);
    printf("body:   %.*s\n", (int)ctx.body_len, ctx.body);
    printf("status: %s\n", ctx.msg_done ? "complete" : "incomplete");
    return 0;
}
