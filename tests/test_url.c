/*
 * lightning - URL parser tests.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "harness.h"
#include "lightning/lightning.h"

LHT_TEST(url_origin_form)
{
    lht_url_t u;
    LHT_CHECK(lht_parse_url("/path/to?a=1&b=2#frag", 21, &u, 0) == LHT_OK);
    LHT_CHECK(u.scheme_len == 0);
    LHT_CHECK_STR(u.path, u.path_len, "/path/to");
    LHT_CHECK_STR(u.query, u.query_len, "a=1&b=2");
    LHT_CHECK_STR(u.fragment, u.fragment_len, "frag");
}

LHT_TEST(url_absolute)
{
    lht_url_t u;
    LHT_CHECK(lht_parse_url("https://user:pw@example.com:8080/p?q=1#x", 40, &u, 0) == LHT_OK);
    LHT_CHECK_STR(u.scheme, u.scheme_len, "https");
    LHT_CHECK_STR(u.userinfo, u.userinfo_len, "user:pw");
    LHT_CHECK_STR(u.host, u.host_len, "example.com");
    LHT_CHECK_STR(u.port, u.port_len, "8080");
    LHT_CHECK_STR(u.path, u.path_len, "/p");
    LHT_CHECK_STR(u.query, u.query_len, "q=1");
    LHT_CHECK_STR(u.fragment, u.fragment_len, "x");
}

LHT_TEST(url_ipv6)
{
    lht_url_t u;
    LHT_CHECK(lht_parse_url("http://[::1]:8080/p", 19, &u, 0) == LHT_OK);
    LHT_CHECK_STR(u.host, u.host_len, "::1");
    LHT_CHECK_STR(u.port, u.port_len, "8080");
}

LHT_TEST(url_connect)
{
    lht_url_t u;
    LHT_CHECK(lht_parse_url("example.com:443", 15, &u, 1) == LHT_OK);
    LHT_CHECK_STR(u.host, u.host_len, "example.com");
    LHT_CHECK_STR(u.port, u.port_len, "443");
}

LHT_TEST(percent_decode)
{
    char out[64];
    size_t n = lht_percent_decode("hello%20world%21", 16, out);
    out[n] = 0;
    LHT_CHECK_STR(out, n, "hello world!");

    n = lht_percent_decode("a+b+c", 5, out);
    out[n] = 0;
    LHT_CHECK_STR(out, n, "a b c");

    n = lht_percent_decode("%ZZ", 3, out);
    out[n] = 0;
    LHT_CHECK_STR(out, n, "%ZZ"); /* malformed -> verbatim */
}

LHT_TEST(parse_query)
{
    lht_kv_t kv[8];
    size_t n = lht_parse_query("a=1&b=2&c=&d", 13, kv, 8);
    LHT_CHECK_EQ(n, 4);
    LHT_CHECK_STR(kv[0].key, kv[0].key_len, "a");
    LHT_CHECK_STR(kv[0].value, kv[0].value_len, "1");
    LHT_CHECK_STR(kv[1].key, kv[1].key_len, "b");
    LHT_CHECK_STR(kv[2].value, kv[2].value_len, "");
    LHT_CHECK(kv[3].value == NULL);
}

static void run_all(void)
{
    LHT_RUN(url_origin_form);
    LHT_RUN(url_absolute);
    LHT_RUN(url_ipv6);
    LHT_RUN(url_connect);
    LHT_RUN(percent_decode);
    LHT_RUN(parse_query);
}

LHT_TEST_MAIN()
