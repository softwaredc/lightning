/*
 * lightning - HTTP parser benchmark.
 *
 * Compares lightning against picohttpparser, http_parser, and llhttp
 * (when available) on realistic request/response workloads.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _POSIX_C_SOURCE 199309L
#include "lightning/lightning.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if __has_include("picohttpparser.h")
#  include "picohttpparser.h"
#  define HAVE_PICO 1
#else
#  define HAVE_PICO 0
#endif

#if __has_include("http_parser.h")
#  include "http_parser.h"
#  define HAVE_HTTPPARSER 1
#else
#  define HAVE_HTTPPARSER 0
#endif

#if __has_include("llhttp.h")
#  include "llhttp.h"
#  define HAVE_LLHTTP 1
#else
#  define HAVE_LLHTTP 0
#endif

/* ---- realistic workloads (captured-ish, representative) ---- */

static const char *g_requests[] = {
    "GET /index.html HTTP/1.1\r\n"
    "Host: example.com\r\n"
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36\r\n"
    "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
    "Accept-Language: en-US,en;q=0.5\r\n"
    "Accept-Encoding: gzip, deflate, br\r\n"
    "Connection: keep-alive\r\n"
    "Upgrade-Insecure-Requests: 1\r\n"
    "\r\n",

    "POST /api/v1/users/42/orders HTTP/1.1\r\n"
    "Host: api.example.com\r\n"
    "User-Agent: curl/8.4.0\r\n"
    "Accept: application/json\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: 76\r\n"
    "Authorization: Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.payload.sig\r\n"
    "X-Request-ID: 9f8e7d6c-5b4a-3210-fedc-ba9876543210\r\n"
    "\r\n"
    "{\"item_id\":12345,\"quantity\":2,\"currency\":\"USD\",\"address\":{\"city\":\"NY\"}}",

    "GET /static/js/app.bundle.js?v=1.2.3 HTTP/1.1\r\n"
    "Host: cdn.example.net\r\n"
    "User-Agent: Mozilla/5.0\r\n"
    "Accept: */*\r\n"
    "Accept-Encoding: gzip, deflate\r\n"
    "If-None-Match: \"abc123def456\"\r\n"
    "If-Modified-Since: Wed, 21 Oct 2025 07:28:00 GMT\r\n"
    "Cache-Control: max-age=0\r\n"
    "Connection: keep-alive\r\n"
    "\r\n",
};

static const char *g_responses[] = {
    "HTTP/1.1 200 OK\r\n"
    "Server: nginx/1.25.3\r\n"
    "Date: Wed, 02 Oct 2026 12:00:00 GMT\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Content-Length: 1256\r\n"
    "Connection: keep-alive\r\n"
    "Vary: Accept-Encoding\r\n"
    "Cache-Control: public, max-age=3600\r\n"
    "ETag: \"abc123\"\r\n"
    "\r\n"
    "<!DOCTYPE html><html><head><title>Example</title></head><body>Hello</body></html>",

    "HTTP/1.1 201 Created\r\n"
    "Server: api-server/2.0\r\n"
    "Date: Wed, 02 Oct 2026 12:00:01 GMT\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: 64\r\n"
    "Location: /api/v1/orders/98765\r\n"
    "X-Request-ID: 9f8e7d6c-5b4a-3210-fedc-ba9876543210\r\n"
    "Connection: keep-alive\r\n"
    "\r\n"
    "{\"order_id\":98765,\"status\":\"created\",\"total\":\"49.98 USD\"}",

    "HTTP/1.1 304 Not Modified\r\n"
    "Server: nginx/1.25.3\r\n"
    "Date: Wed, 02 Oct 2026 12:00:02 GMT\r\n"
    "Connection: keep-alive\r\n"
    "ETag: \"abc123def456\"\r\n"
    "Cache-Control: public, max-age=3600\r\n"
    "\r\n",
};

#define NREQ (sizeof(g_requests) / sizeof(g_requests[0]))
#define NRES (sizeof(g_responses) / sizeof(g_responses[0]))

static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static int cmp_int(const void *a, const void *b)
{
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}

/* ---- lightning parse (one-shot) ---- */
static size_t bench_lightning(const char **msgs, size_t n, int iters)
{
    lht_header_t hdrs[32];
    size_t parsed = 0;
    for (int it = 0; it < iters; ++it) {
        for (size_t i = 0; i < n; ++i) {
            const char *buf = msgs[i];
            size_t len = strlen(buf);
            const char *m, *p;
            size_t ml, pl;
            int minor;
            size_t nh = 32;
            int r = lht_parse_request(buf, len, &m, &ml, &p, &pl, &minor, hdrs, &nh);
            if (r >= 0) parsed++;
        }
    }
    return parsed;
}

static size_t bench_lightning_resp(const char **msgs, size_t n, int iters)
{
    lht_header_t hdrs[32];
    size_t parsed = 0;
    for (int it = 0; it < iters; ++it) {
        for (size_t i = 0; i < n; ++i) {
            const char *buf = msgs[i];
            size_t len = strlen(buf);
            int status, minor;
            const char *reason;
            size_t rlen;
            size_t nh = 32;
            int r = lht_parse_response(buf, len, &status, &reason, &rlen, &minor, hdrs, &nh);
            if (r >= 0) parsed++;
        }
    }
    return parsed;
}

#if HAVE_PICO
static size_t bench_pico(const char **msgs, size_t n, int iters)
{
    struct phr_header hdrs[32];
    size_t parsed = 0;
    for (int it = 0; it < iters; ++it) {
        for (size_t i = 0; i < n; ++i) {
            const char *buf = msgs[i];
            size_t len = strlen(buf);
            const char *method, *path;
            size_t ml, pl;
            int minor;
            size_t nh = 32;
            int r = phr_parse_request(buf, len, &method, &ml, &path, &pl, &minor, hdrs, &nh, 0);
            if (r >= 0) parsed++;
        }
    }
    return parsed;
}

static size_t bench_pico_resp(const char **msgs, size_t n, int iters)
{
    struct phr_header hdrs[32];
    size_t parsed = 0;
    for (int it = 0; it < iters; ++it) {
        for (size_t i = 0; i < n; ++i) {
            const char *buf = msgs[i];
            size_t len = strlen(buf);
            int status, minor;
            const char *msg;
            size_t mlen;
            size_t nh = 32;
            int r = phr_parse_response(buf, len, &minor, &status, &msg, &mlen, hdrs, &nh, 0);
            if (r >= 0) parsed++;
        }
    }
    return parsed;
}
#endif

#if HAVE_HTTPPARSER
static int hp_on_url(http_parser *hp, const char *at, size_t len) { (void)hp;(void)at;(void)len; return 0; }
static int hp_on_hf(http_parser *hp, const char *at, size_t len) { (void)hp;(void)at;(void)len; return 0; }
static int hp_on_hv(http_parser *hp, const char *at, size_t len) { (void)hp;(void)at;(void)len; return 0; }
static int hp_on_body(http_parser *hp, const char *at, size_t len) { (void)hp;(void)at;(void)len; return 0; }
static int hp_noop(http_parser *hp) { (void)hp; return 0; }

static size_t bench_httpparser(const char **msgs, size_t n, int iters)
{
    http_parser_settings s;
    memset(&s, 0, sizeof(s));
    s.on_url = hp_on_url;
    s.on_header_field = hp_on_hf;
    s.on_header_value = hp_on_hv;
    s.on_body = hp_on_body;
    s.on_message_begin = hp_noop;
    s.on_headers_complete = hp_noop;
    s.on_message_complete = hp_noop;
    size_t parsed = 0;
    for (int it = 0; it < iters; ++it) {
        for (size_t i = 0; i < n; ++i) {
            http_parser hp;
            http_parser_init(&hp, HTTP_REQUEST);
            const char *buf = msgs[i];
            size_t len = strlen(buf);
            size_t r = http_parser_execute(&hp, &s, buf, len);
            if (r == len) parsed++;
        }
    }
    return parsed;
}
#endif

#if HAVE_LLHTTP
static int ll_on_url(llhttp_t *p, const char *at, size_t len) { (void)p;(void)at;(void)len; return 0; }
static int ll_on_hf(llhttp_t *p, const char *at, size_t len) { (void)p;(void)at;(void)len; return 0; }
static int ll_on_hv(llhttp_t *p, const char *at, size_t len) { (void)p;(void)at;(void)len; return 0; }
static int ll_on_body(llhttp_t *p, const char *at, size_t len) { (void)p;(void)at;(void)len; return 0; }

static size_t bench_llhttp(const char **msgs, size_t n, int iters)
{
    llhttp_settings_t s;
    memset(&s, 0, sizeof(s));
    s.on_url = ll_on_url;
    s.on_header_field = ll_on_hf;
    s.on_header_value = ll_on_hv;
    s.on_body = ll_on_body;
    size_t parsed = 0;
    for (int it = 0; it < iters; ++it) {
        for (size_t i = 0; i < n; ++i) {
            llhttp_t hp;
            llhttp_init(&hp, HTTP_REQUEST, &s);
            const char *buf = msgs[i];
            size_t len = strlen(buf);
            enum llhttp_errno e = llhttp_execute(&hp, buf, len);
            if (e == HPE_OK) parsed++;
        }
    }
    return parsed;
}
#endif

typedef size_t (*bench_fn_t)(const char **, size_t, int);

static void run_bench(const char *name, const char **msgs, size_t n,
                      bench_fn_t fn, int iters, double *total_bytes)
{
    /* warmup */
    fn(msgs, n, 5);

    double times[7];
    for (int r = 0; r < 7; ++r) {
        double t0 = now_sec();
        size_t p = fn(msgs, n, iters);
        double t1 = now_sec();
        times[r] = (double)p / (t1 - t0);  /* msgs/sec */
        (void)p;
    }
    qsort(times, 7, sizeof(double), cmp_int);
    double median = times[3];

    size_t bytes_per_msg = 0;
    for (size_t i = 0; i < n; ++i) bytes_per_msg += strlen(msgs[i]);
    double gbps = median * bytes_per_msg / 1e9;

    printf("  %-18s %12.0f msgs/s   %8.2f GB/s\n", name, median, gbps);
    *total_bytes += bytes_per_msg * iters;
}

int main(void)
{
    printf("lightning HTTP parser benchmark\n");
    printf("SIMD backend: %s\n", lht_simd_backend());
    printf("CPU: ");
    fflush(stdout);
    (void)system("grep -m1 'model name' /proc/cpuinfo | cut -d: -f2 | xargs");
    printf("\n");

    int iters = 200000;

    printf("=== Request parsing (%zu messages, %d iters) ===\n", NREQ, iters);
    double tb = 0;
    run_bench("lightning", g_requests, NREQ, bench_lightning, iters, &tb);
#if HAVE_PICO
    run_bench("picohttpparser", g_requests, NREQ, bench_pico, iters, &tb);
#endif
#if HAVE_HTTPPARSER
    run_bench("http_parser", g_requests, NREQ, bench_httpparser, iters, &tb);
#endif
#if HAVE_LLHTTP
    run_bench("llhttp", g_requests, NREQ, bench_llhttp, iters, &tb);
#endif

    printf("\n=== Response parsing (%zu messages, %d iters) ===\n", NRES, iters);
    run_bench("lightning", g_responses, NRES, bench_lightning_resp, iters, &tb);
#if HAVE_PICO
    run_bench("picohttpparser", g_responses, NRES, bench_pico_resp, iters, &tb);
#endif

    printf("\nBackend detection: SSE4.2=%d AVX2=%d\n", lht_has_sse42(), lht_has_avx2());
    return 0;
}
