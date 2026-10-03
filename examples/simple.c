/*
 * lightning - minimal example: one-shot request parsing.
 *
 * Build:  cc -I../include simple.c -L../build -llightning -o simple
 * SPDX-License-Identifier: Apache-2.0
 */
#include "lightning/lightning.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char *req =
        "GET /api/users?active=1 HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "Accept: application/json\r\n"
        "User-Agent: curl/8.0\r\n"
        "\r\n";

    const char *method, *path;
    size_t mlen, plen;
    int minor;
    lht_header_t hdrs[16];
    size_t nh = 16;

    int body_off = lht_parse_request(req, strlen(req),
                                     &method, &mlen, &path, &plen,
                                     &minor, hdrs, &nh);
    if (body_off < 0) {
        fprintf(stderr, "parse error: %d\n", body_off);
        return 1;
    }

    printf("method: %.*s\n", (int)mlen, method);
    printf("path:   %.*s\n", (int)plen, path);
    printf("HTTP/1.%d\n", minor);
    printf("headers (%zu):\n", nh);
    for (size_t i = 0; i < nh; ++i)
        printf("  %.*s: %.*s\n",
               (int)hdrs[i].name_len, hdrs[i].name,
               (int)hdrs[i].value_len, hdrs[i].value);
    printf("body offset: %d\n", body_off);

    /* header lookup is case-insensitive and zero-copy */
    int idx = lht_find_header(hdrs, nh, "accept", 6);
    if (idx >= 0)
        printf("Accept => %.*s\n", (int)hdrs[idx].value_len, hdrs[idx].value);

    return 0;
}
