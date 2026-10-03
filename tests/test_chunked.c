/*
 * lightning - chunked transfer-encoding tests.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "harness.h"
#include "lightning/lightning.h"

LHT_TEST(chunked_basic)
{
    const char *in = "5\r\nHello\r\n6\r\n World\r\n0\r\n\r\n";
    size_t in_len = strlen(in);
    lht_chunked_t c;
    lht_chunked_init(&c);

    char out[256];
    size_t off = 0;
    const char *o;
    size_t olen;
    const char *p = in;
    size_t remain = in_len;
    while (remain > 0) {
        size_t consumed = lht_chunked_execute(&c, p, remain, &o, &olen);
        LHT_CHECK(consumed != (size_t)-1);
        if (olen) {
            memcpy(out + off, o, olen);
            off += olen;
        }
        p += consumed;
        remain -= consumed;
        if (c.done) break;
    }
    out[off] = 0;
    LHT_CHECK_STR(out, off, "Hello World");
    LHT_CHECK(c.done);
}

LHT_TEST(chunked_incremental)
{
    /* feed one byte at a time */
    const char *in = "3\r\nabc\r\n0\r\n\r\n";
    size_t in_len = strlen(in);
    lht_chunked_t c;
    lht_chunked_init(&c);
    char out[64]; size_t off = 0;
    const char *o; size_t olen;
    for (size_t i = 0; i < in_len; ++i) {
        size_t consumed = lht_chunked_execute(&c, &in[i], 1, &o, &olen);
        LHT_CHECK(consumed == 1);
        if (olen) { memcpy(out + off, o, olen); off += olen; }
    }
    out[off] = 0;
    LHT_CHECK_STR(out, off, "abc");
}

LHT_TEST(chunked_empty)
{
    const char *in = "0\r\n\r\n";
    lht_chunked_t c;
    lht_chunked_init(&c);
    const char *o; size_t olen;
    size_t consumed = lht_chunked_execute(&c, in, strlen(in), &o, &olen);
    LHT_CHECK(consumed == strlen(in));
    LHT_CHECK(c.done);
}

static void run_all(void)
{
    LHT_RUN(chunked_basic);
    LHT_RUN(chunked_incremental);
    LHT_RUN(chunked_empty);
}

LHT_TEST_MAIN()
