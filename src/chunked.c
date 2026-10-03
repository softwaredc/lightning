/*
 * lightning - chunked transfer-encoding decoder.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "internal.h"

void lht_chunked_init(lht_chunked_t *c)
{
    memset(c, 0, sizeof(*c));
}

/*
 * States:
 *   0 = reading chunk size
 *   1 = reading chunk params / line terminator
 *   2 = (reserved)
 *   3 = expect LF after size line
 *   4 = reading chunk data
 *   5 = expect CR after data
 *   6 = expect LF after data
 *   7 = consuming final message terminator (after zero chunk)
 *   8 = fully done
 */
size_t lht_chunked_execute(lht_chunked_t *c, const char *in, size_t in_len,
                           const char **out, size_t *out_len)
{
    const char *p = in;
    const char *end = in + in_len;
    *out = NULL;
    *out_len = 0;

    while (p < end) {
        switch (c->state) {
        case 0: { /* chunk size (hex) */
            const char *start = p;
            uint64_t sz = c->size;
            while (p < end) {
                int h = lht_hexval((unsigned char)*p);
                if (h < 0) break;
                if (sz > (UINT64_MAX >> 4)) return (size_t)-1; /* overflow */
                sz = (sz << 4) | (uint64_t)h;
                ++p;
            }
            c->size = sz;
            if (p == start) return (size_t)-1; /* no hex digits */
            c->state = 1;
            break;
        }
        case 1: /* chunk extensions: skip to and consume line terminator */
            while (p < end && *p != '\r' && *p != '\n') ++p;
            if (p == end) return p - in;
            if (*p == '\r') { ++p; c->state = 3; }  /* consume CR, expect LF */
            else            { ++p; c->state = 4; goto enter_data; }  /* bare LF */
            break;
        case 2: /* not used */
            c->state = 3; break;
        case 3:
            if (*p != '\n') return (size_t)-1;
            ++p;
        enter_data:
            if (c->size == 0) {
                /* zero chunk: consume optional trailers + final CRLF */
                c->state = 7;
                break;
            }
            c->remaining = c->size;
            c->state = 4;
            break;
        case 4: { /* chunk data */
            size_t avail = (size_t)(end - p);
            size_t take = (avail < (size_t)c->remaining)
                              ? avail : (size_t)c->remaining;
            if (take > 0) {
                *out = p;
                *out_len = take;
                p += take;
                c->remaining -= take;
                if (c->remaining == 0)
                    c->state = 5;
                return p - in;
            }
            break;
        }
        case 5:
            if (*p != '\r') return (size_t)-1;
            ++p; c->state = 6; break;
        case 6:
            if (*p != '\n') return (size_t)-1;
            ++p;
            c->size = 0;
            c->state = 0;
            break;
        case 7: /* consume final message terminator (and any trailers) */
            /* skip trailer lines until blank line */
            while (p < end) {
                if (*p == '\r') {
                    ++p;
                    if (p < end && *p == '\n') {
                        ++p;
                        c->state = 8;
                        c->done = 1;
                        return p - in;
                    }
                    /* CR without LF - keep scanning */
                } else if (*p == '\n') {
                    ++p;
                    c->state = 8;
                    c->done = 1;
                    return p - in;
                } else {
                    /* trailer line: skip to end of line */
                    while (p < end && *p != '\r' && *p != '\n') ++p;
                    if (p == end) return p - in;
                }
            }
            return p - in;
        case 8:
            return p - in;
        default:
            return (size_t)-1;
        }
    }
    return p - in;
}
