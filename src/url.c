/*
 * lightning - URL parser (RFC 3986) and percent-decoder.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "internal.h"

int lht_parse_url(const char *url, size_t len, lht_url_t *out, int is_connect)
{
    if (!url || !out) return LHT_EINVAL;
    memset(out, 0, sizeof(*out));

    const char *p = url;
    const char *end = url + len;

    if (len == 0) return LHT_EPARSE;

    if (is_connect) {
        /* authority form: host:port */
        out->host = p;
        const char *colon = NULL;
        for (const char *q = p; q < end; ++q) {
            if (*q == ':') { colon = q; break; }
        }
        if (colon) {
            out->host_len = (size_t)(colon - p);
            out->port = colon + 1;
            out->port_len = (size_t)(end - colon - 1);
        } else {
            out->host_len = len;
        }
        return (out->host_len == 0) ? LHT_EPARSE : LHT_OK;
    }

    /* scheme? */
    const char *colon = NULL;
    for (const char *q = p; q < end; ++q) {
        if (*q == ':') { colon = q; break; }
        if (*q == '/' || *q == '?' || *q == '#') break;
    }
    if (colon && colon > p) {
        /* validate scheme: ALPHA *( ALPHA / DIGIT / + / - / . ) */
        char first = *p;
        if (!((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z')))
            colon = NULL;
    }
    if (colon) {
        out->scheme = p;
        out->scheme_len = (size_t)(colon - p);
        p = colon + 1;
        /* authority? */
        if (end - p >= 2 && p[0] == '/' && p[1] == '/') {
            p += 2;
            /* authority: [ userinfo@ ] host [ :port ] */
            const char *auth_end = p;
            while (auth_end < end && *auth_end != '/' &&
                   *auth_end != '?' && *auth_end != '#')
                ++auth_end;
            /* userinfo */
            const char *at = NULL;
            for (const char *q = p; q < auth_end; ++q)
                if (*q == '@') at = q;
            if (at) {
                out->userinfo = p;
                out->userinfo_len = (size_t)(at - p);
                p = at + 1;
            }
            /* host:port */
            const char *port_colon = NULL;
            if (p < auth_end && *p == '[') {
                /* IPv6 literal */
                const char *rb = p;
                while (rb < auth_end && *rb != ']') ++rb;
                if (rb == auth_end) return LHT_EPARSE;
                out->host = p + 1;
                out->host_len = (size_t)(rb - p - 1);
                p = rb + 1;
                if (p < auth_end && *p == ':') {
                    out->port = p + 1;
                    out->port_len = (size_t)(auth_end - p - 1);
                }
            } else {
                for (const char *q = p; q < auth_end; ++q)
                    if (*q == ':') port_colon = q;
                if (port_colon) {
                    out->host = p;
                    out->host_len = (size_t)(port_colon - p);
                    out->port = port_colon + 1;
                    out->port_len = (size_t)(auth_end - port_colon - 1);
                } else {
                    out->host = p;
                    out->host_len = (size_t)(auth_end - p);
                }
            }
            p = auth_end;
        }
    }

    /* path */
    if (p < end && *p != '?' && *p != '#') {
        out->path = p;
        while (p < end && *p != '?' && *p != '#') ++p;
        out->path_len = (size_t)(p - out->path);
    }

    /* query */
    if (p < end && *p == '?') {
        ++p;
        out->query = p;
        while (p < end && *p != '#') ++p;
        out->query_len = (size_t)(p - out->query);
    }

    /* fragment */
    if (p < end && *p == '#') {
        ++p;
        out->fragment = p;
        out->fragment_len = (size_t)(end - p);
    }

    return LHT_OK;
}

size_t lht_percent_decode(const char *in, size_t len, char *out)
{
    char *o = out;
    for (size_t i = 0; i < len; ++i) {
        if (in[i] == '%' && i + 2 < len) {
            int hi = lht_hexval((unsigned char)in[i + 1]);
            int lo = lht_hexval((unsigned char)in[i + 2]);
            if (hi >= 0 && lo >= 0) {
                *o++ = (char)((hi << 4) | lo);
                i += 2;
                continue;
            }
        } else if (in[i] == '+') {
            *o++ = ' ';
            continue;
        }
        *o++ = in[i];
    }
    return (size_t)(o - out);
}

size_t lht_parse_query(const char *query, size_t len,
                       lht_kv_t *pairs, size_t max_pairs)
{
    if (!query || !pairs || max_pairs == 0) return 0;
    size_t count = 0;
    const char *p = query;
    const char *end = query + len;
    while (p < end && count < max_pairs) {
        const char *amp = p;
        while (amp < end && *amp != '&') ++amp;
        const char *eq = p;
        while (eq < amp && *eq != '=') ++eq;
        pairs[count].key = p;
        pairs[count].key_len = (size_t)(eq - p);
        if (eq < amp) {
            pairs[count].value = eq + 1;
            pairs[count].value_len = (size_t)(amp - eq - 1);
        } else {
            pairs[count].value = NULL;
            pairs[count].value_len = 0;
        }
        ++count;
        p = amp + 1;
    }
    return count;
}
