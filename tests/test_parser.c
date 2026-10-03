/*
 * lightning - one-shot parser tests.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "harness.h"
#include "lightning/lightning.h"

LHT_TEST(parse_basic_get)
{
    const char *req =
        "GET /index.html HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "User-Agent: curl/8.0\r\n"
        "\r\n";
    size_t len = strlen(req);
    const char *method, *path;
    size_t mlen, plen;
    int minor;
    lht_header_t hdrs[8];
    size_t nh = 8;

    int r = lht_parse_request(req, len, &method, &mlen, &path, &plen,
                              &minor, hdrs, &nh);
    LHT_CHECK(r >= 0);
    LHT_CHECK_STR(method, mlen, "GET");
    LHT_CHECK_STR(path, plen, "/index.html");
    LHT_CHECK_EQ(minor, 1);
    LHT_CHECK_EQ(nh, 2);
    LHT_CHECK_STR(hdrs[0].name, hdrs[0].name_len, "Host");
    LHT_CHECK_STR(hdrs[0].value, hdrs[0].value_len, "example.com");
    LHT_CHECK_STR(hdrs[1].name, hdrs[1].name_len, "User-Agent");
    LHT_CHECK_STR(hdrs[1].value, hdrs[1].value_len, "curl/8.0");
    /* body offset points right after the final CRLF */
    LHT_CHECK_EQ((size_t)r, len);
}

LHT_TEST(parse_with_body)
{
    const char *req =
        "POST /api HTTP/1.1\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "hello";
    size_t len = strlen(req);
    const char *method, *path;
    size_t mlen, plen;
    int minor;
    lht_header_t hdrs[8];
    size_t nh = 8;
    int r = lht_parse_request(req, len, &method, &mlen, &path, &plen,
                              &minor, hdrs, &nh);
    LHT_CHECK(r >= 0);
    LHT_CHECK_STR(method, mlen, "POST");
    LHT_CHECK_EQ(nh, 1);
    LHT_CHECK_STR(hdrs[0].name, hdrs[0].name_len, "Content-Length");
    LHT_CHECK_STR(hdrs[0].value, hdrs[0].value_len, "5");
    /* body starts at offset r */
    LHT_CHECK_STR(req + r, len - (size_t)r, "hello");
}

LHT_TEST(parse_response)
{
    const char *resp =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 13\r\n"
        "\r\n"
        "Hello, World!";
    size_t len = strlen(resp);
    int status, minor;
    const char *reason;
    size_t rlen;
    lht_header_t hdrs[8];
    size_t nh = 8;
    int r = lht_parse_response(resp, len, &status, &reason, &rlen,
                               &minor, hdrs, &nh);
    LHT_CHECK(r >= 0);
    LHT_CHECK_EQ(status, 200);
    LHT_CHECK_STR(reason, rlen, "OK");
    LHT_CHECK_EQ(minor, 1);
    LHT_CHECK_EQ(nh, 2);
    LHT_CHECK_STR(resp + r, len - (size_t)r, "Hello, World!");
}

LHT_TEST(parse_response_no_reason)
{
    const char *resp = "HTTP/1.1 204\r\n\r\n";
    int status, minor;
    const char *reason;
    size_t rlen;
    lht_header_t hdrs[4];
    size_t nh = 4;
    int r = lht_parse_response(resp, strlen(resp), &status, &reason, &rlen,
                               &minor, hdrs, &nh);
    LHT_CHECK(r >= 0);
    LHT_CHECK_EQ(status, 204);
}

LHT_TEST(parse_malformed)
{
    lht_header_t hdrs[8];
    size_t nh;
    const char *m, *p;
    size_t ml, pl;
    int minor;

    /* missing space */
    nh = 8;
    LHT_CHECK(lht_parse_request("GET/path HTTP/1.1\r\n\r\n", 21,
                                &m, &ml, &p, &pl, &minor, hdrs, &nh) == LHT_EPARSE);
    /* bad version */
    nh = 8;
    LHT_CHECK(lht_parse_request("GET / HTTP/1.x\r\n\r\n", 19,
                                &m, &ml, &p, &pl, &minor, hdrs, &nh) == LHT_EPARSE);
    /* empty */
    nh = 8;
    LHT_CHECK(lht_parse_request("", 0, &m, &ml, &p, &pl, &minor, hdrs, &nh) == LHT_EAGAIN);
}

LHT_TEST(header_overflow)
{
    char buf[4096];
    int n = snprintf(buf, sizeof(buf), "GET / HTTP/1.1\r\n");
    for (int i = 0; i < 20; ++i)
        n += snprintf(buf + n, sizeof(buf) - n, "X-H%d: v\r\n", i);
    n += snprintf(buf + n, sizeof(buf) - n, "\r\n");
    const char *m, *p; size_t ml, pl; int minor;
    lht_header_t hdrs[4];
    size_t nh = 4;
    int r = lht_parse_request(buf, (size_t)n, &m, &ml, &p, &pl,
                              &minor, hdrs, &nh);
    LHT_CHECK_EQ(r, LHT_EOVERFLOW);
}

LHT_TEST(header_ows_trim)
{
    const char *req =
        "GET / HTTP/1.1\r\n"
        "X-Name:    spaced value   \r\n"
        "\r\n";
    const char *m, *p; size_t ml, pl; int minor;
    lht_header_t hdrs[4]; size_t nh = 4;
    int r = lht_parse_request(req, strlen(req), &m, &ml, &p, &pl,
                              &minor, hdrs, &nh);
    LHT_CHECK(r >= 0);
    LHT_CHECK_EQ(nh, 1);
    LHT_CHECK_STR(hdrs[0].name, hdrs[0].name_len, "X-Name");
    LHT_CHECK_STR(hdrs[0].value, hdrs[0].value_len, "spaced value");
}

LHT_TEST(find_header_case_insensitive)
{
    lht_header_t hdrs[2] = {
        {"Content-Type", 12, "text/html", 9},
        {"Set-Cookie", 10, "a=b", 3},
    };
    LHT_CHECK(lht_find_header(hdrs, 2, "content-type", 12) == 0);
    LHT_CHECK(lht_find_header(hdrs, 2, "SET-COOKIE", 10) == 1);
    LHT_CHECK(lht_find_header(hdrs, 2, "missing", 7) == -1);
}

LHT_TEST(header_has_token)
{
    const char *v = "chunked, keep-alive, 100-continue";
    LHT_CHECK(lht_header_has_token(v, strlen(v), "chunked", 7));
    LHT_CHECK(lht_header_has_token(v, strlen(v), "keep-alive", 10));
    LHT_CHECK(lht_header_has_token(v, strlen(v), "100-continue", 12));
    LHT_CHECK(!lht_header_has_token(v, strlen(v), "gzip", 4));
}

LHT_TEST(http10)
{
    const char *req = "GET / HTTP/1.0\r\n\r\n";
    const char *m, *p; size_t ml, pl; int minor;
    lht_header_t hdrs[4]; size_t nh = 4;
    int r = lht_parse_request(req, strlen(req), &m, &ml, &p, &pl,
                              &minor, hdrs, &nh);
    LHT_CHECK(r >= 0);
    LHT_CHECK_EQ(minor, 0);
}

static void run_all(void)
{
    LHT_RUN(parse_basic_get);
    LHT_RUN(parse_with_body);
    LHT_RUN(parse_response);
    LHT_RUN(parse_response_no_reason);
    LHT_RUN(parse_malformed);
    LHT_RUN(header_overflow);
    LHT_RUN(header_ows_trim);
    LHT_RUN(find_header_case_insensitive);
    LHT_RUN(header_has_token);
    LHT_RUN(http10);
}

LHT_TEST_MAIN()
