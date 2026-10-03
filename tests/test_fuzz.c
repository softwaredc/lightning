/*
 * lightning - fuzz / edge-case tests.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "harness.h"
#include "lightning/lightning.h"
#include <time.h>

/* Feed random bytes to the streaming parser; it must never crash or
 * read out of bounds. Returns the number of iterations. */
LHT_TEST(fuzz_streaming)
{
    lht_stream_t s;
    lht_settings_t st; memset(&st, 0, sizeof(st));
    srand(12345);
    for (int iter = 0; iter < 5000; ++iter) {
        lht_stream_init(&s, (iter & 1) ? LHT_MSG_RESPONSE : LHT_MSG_REQUEST);
        size_t len = (size_t)(rand() % 512);
        char buf[512];
        for (size_t i = 0; i < len; ++i)
            buf[i] = (char)(rand() & 0xff);
        /* must not crash */
        size_t consumed = lht_stream_execute(&s, &st, buf, len);
        LHT_CHECK(consumed <= len);
    }
}

/* Valid request template + byte flips: parser must be robust. */
LHT_TEST(fuzz_valid_then_mutate)
{
    const char *tmpl =
        "GET /path?x=1 HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "Content-Length: 4\r\n"
        "\r\n"
        "body";
    size_t tlen = strlen(tmpl);
    lht_stream_t s;
    lht_settings_t st; memset(&st, 0, sizeof(st));
    srand(54321);
    for (int iter = 0; iter < 3000; ++iter) {
        char buf[256];
        memcpy(buf, tmpl, tlen);
        int nmut = rand() % 4;
        for (int j = 0; j < nmut; ++j)
            buf[rand() % tlen] = (char)(rand() & 0xff);
        lht_stream_init(&s, LHT_MSG_REQUEST);
        lht_stream_execute(&s, &st, buf, tlen);
        /* must not crash */
    }
}

/* One-shot parser must never read past the buffer on truncated input. */
LHT_TEST(fuzz_oneshot_truncated)
{
    const char *full = "POST /upload HTTP/1.1\r\nContent-Length: 3\r\n\r\nabc";
    size_t flen = strlen(full);
    lht_header_t hdrs[8];
    const char *m, *p; size_t ml, pl; int minor;
    for (size_t cut = 1; cut < flen; ++cut) {
        size_t nh = 8;
        int r = lht_parse_request(full, cut, &m, &ml, &p, &pl, &minor, hdrs, &nh);
        /* either error or valid offset within [0, cut] */
        LHT_CHECK(r < 0 || (size_t)r <= cut);
    }
}

LHT_TEST(large_header_block)
{
    /* 512 headers */
    char *buf = malloc(1 << 20);
    int n = snprintf(buf, 1 << 20, "GET / HTTP/1.1\r\n");
    for (int i = 0; i < 512; ++i)
        n += snprintf(buf + n, (1 << 20) - n, "X-Hdr-%03d: value%d\r\n", i, i);
    n += snprintf(buf + n, (1 << 20) - n, "\r\n");
    const char *m, *p; size_t ml, pl; int minor;
    lht_header_t *hdrs = calloc(600, sizeof(lht_header_t));
    size_t nh = 600;
    int r = lht_parse_request(buf, (size_t)n, &m, &ml, &p, &pl, &minor, hdrs, &nh);
    LHT_CHECK(r >= 0);
    LHT_CHECK_EQ(nh, 512);
    free(hdrs);
    free(buf);
}

static void run_all(void)
{
    LHT_RUN(fuzz_streaming);
    LHT_RUN(fuzz_valid_then_mutate);
    LHT_RUN(fuzz_oneshot_truncated);
    LHT_RUN(large_header_block);
}

LHT_TEST_MAIN()
