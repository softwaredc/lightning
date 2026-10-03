/*
 * lightning - HTTP/1.1 parser core.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "internal.h"

/* ------------------------------------------------------------------ */
/* CPU feature detection (runtime)                                     */
/* ------------------------------------------------------------------ */

#if defined(LHT_X86)

static void lht_cpuid(uint32_t leaf, uint32_t subleaf,
                      uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
    __asm__ __volatile__(
        "cpuid"
        : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
        : "a"(leaf), "c"(subleaf));
}

static int lht_has_osxsave(void)
{
    uint32_t a, b, c, d;
    lht_cpuid(1, 0, &a, &b, &c, &d);
    return (c & (1u << 27)) != 0;  /* OSXSAVE */
}

static int lht_has_avx2_runtime(void)
{
    if (!lht_has_osxsave())
        return 0;
    /* check OS enabled AVX state via xgetbv */
    uint32_t xcr0_lo, xcr0_hi;
    __asm__ __volatile__("xgetbv" : "=a"(xcr0_lo), "=d"(xcr0_hi) : "c"(0));
    if ((xcr0_lo & 6) != 6)  /* SSE + AVX state */
        return 0;
    uint32_t a, b, c, d;
    lht_cpuid(7, 0, &a, &b, &c, &d);
    return (b & (1u << 5)) != 0;  /* AVX2 */
}

static int lht_has_sse42_runtime(void)
{
    uint32_t a, b, c, d;
    lht_cpuid(1, 0, &a, &b, &c, &d);
    return (c & (1u << 20)) != 0;  /* SSE4.2 */
}

#else
static int lht_has_avx2_runtime(void)  { return 0; }
static int lht_has_sse42_runtime(void) { return 0; }
#endif

static int g_avx2  = -1;
static int g_sse42 = -1;

int lht_has_sse42(void)
{
    if (g_sse42 < 0) g_sse42 = lht_has_sse42_runtime() ? 1 : 0;
    return g_sse42;
}

int lht_has_avx2(void)
{
    if (g_avx2 < 0) g_avx2 = lht_has_avx2_runtime() ? 1 : 0;
    return g_avx2;
}

const char *lht_simd_backend(void)
{
    if (lht_has_avx2())  return "avx2";
    if (lht_has_sse42()) return "sse42";
    return "scalar";
}

/* Find first line terminator. Every HTTP/1 line ends with LF (\n); CR is
 * optional and immediately precedes it. A single memchr for '\n' is enough. */
static const char *find_nl(const char *p, size_t len)
{
    const char *nl = memchr(p, '\n', len);
    return nl ? nl : p + len;
}

/* Find first colon in a header field name (field names cannot contain \n). */
static const char *find_colon(const char *p, size_t len)
{
    const char *c = memchr(p, ':', len);
    return c ? c : p + len;
}

/* ------------------------------------------------------------------ */
/* Method table                                                        */
/* ------------------------------------------------------------------ */

static const char *const k_methods[] = {
    [LHT_M_DELETE]  = "DELETE",
    [LHT_M_GET]     = "GET",
    [LHT_M_HEAD]    = "HEAD",
    [LHT_M_POST]    = "POST",
    [LHT_M_PUT]     = "PUT",
    [LHT_M_CONNECT] = "CONNECT",
    [LHT_M_OPTIONS] = "OPTIONS",
    [LHT_M_TRACE]   = "TRACE",
    [LHT_M_PATCH]   = "PATCH",
};

const char *lht_method_str(int method)
{
    if (method > 0 && method < (int)(sizeof(k_methods) / sizeof(k_methods[0])))
        return k_methods[method];
    return NULL;
}

int lht_method_match(const lht_stream_t *s, const char *m, size_t mlen)
{
    const char *name = lht_method_str(s->method);
    if (!name)
        return 0;
    return lht_ascii_ieq(name, strlen(name), m, mlen);
}

static int method_index(const char *p, size_t len)
{
    switch (len) {
    case 3:
        if (lht_ascii_ieq(p, len, "GET", 3)) return LHT_M_GET;
        if (lht_ascii_ieq(p, len, "PUT", 3)) return LHT_M_PUT;
        break;
    case 4:
        if (lht_ascii_ieq(p, len, "POST", 4)) return LHT_M_POST;
        if (lht_ascii_ieq(p, len, "HEAD", 4)) return LHT_M_HEAD;
        break;
    case 5:
        if (lht_ascii_ieq(p, len, "PATCH", 5)) return LHT_M_PATCH;
        if (lht_ascii_ieq(p, len, "TRACE", 5)) return LHT_M_TRACE;
        break;
    case 6:
        if (lht_ascii_ieq(p, len, "DELETE", 6)) return LHT_M_DELETE;
        break;
    case 7:
        if (lht_ascii_ieq(p, len, "OPTIONS", 7)) return LHT_M_OPTIONS;
        if (lht_ascii_ieq(p, len, "CONNECT", 7)) return LHT_M_CONNECT;
        break;
    }
    return LHT_M__OTHER;
}

/* ------------------------------------------------------------------ */
/* One-shot header block parser                                        */
/* ------------------------------------------------------------------ */

/* Parse `Name: Value\r\n` lines starting at `start`.
 * Returns offset just past the terminating blank line, or negative error. */
int lht_parse_headers(const char *buf, size_t len, size_t start,
                      lht_header_t *headers, size_t *num_headers)
{
    if (!buf || !headers || !num_headers)
        return LHT_EINVAL;
    size_t cap = *num_headers;
    size_t count = 0;
    const char *p = buf + start;
    const char *end = buf + len;

    while (p < end) {
        /* blank line -> end of headers */
        if (*p == '\r' || *p == '\n') {
            if (*p == '\r') ++p;
            if (p < end && *p == '\n') ++p;
            *num_headers = count;
            return (int)(p - buf);
        }
        if (count >= cap) {
            *num_headers = count;
            return LHT_EOVERFLOW;
        }

        /* field name: up to ':' */
        const char *colon = find_colon(p, (size_t)(end - p));
        if (colon >= end) {
            return LHT_EPARSE;
        }
        /* trim trailing OWS from name */
        const char *name_end = colon;
        while (name_end > p && (name_end[-1] == ' ' || name_end[-1] == '\t'))
            --name_end;

        headers[count].name = p;
        headers[count].name_len = (size_t)(name_end - p);

        /* value: after ':', skip leading OWS */
        const char *vstart = colon + 1;
        while (vstart < end && (*vstart == ' ' || *vstart == '\t'))
            ++vstart;

        /* find end of line */
        const char *nl = find_nl(vstart, (size_t)(end - vstart));
        if (nl >= end)
            return LHT_EPARSE;  /* malformed: no newline */
        /* trim trailing OWS (and optional CR before LF) */
        const char *vend = nl;
        while (vend > vstart &&
               (vend[-1] == ' ' || vend[-1] == '\t' || vend[-1] == '\r'))
            --vend;

        headers[count].value = vstart;
        headers[count].value_len = (size_t)(vend - vstart);
        ++count;

        /* skip past the LF (find_nl points at '\n') */
        p = nl + 1;
    }

    *num_headers = count;
    return LHT_EAGAIN;  /* ran out of data before blank line */
}

/* ------------------------------------------------------------------ */
/* One-shot request parser                                             */
/* ------------------------------------------------------------------ */

int lht_parse_request(const char *buf, size_t len,
                      const char **method, size_t *method_len,
                      const char **path, size_t *path_len,
                      int *minor_version,
                      lht_header_t *headers, size_t *num_headers)
{
    if (!buf || !method || !method_len || !path || !path_len ||
        !minor_version || !headers || !num_headers)
        return LHT_EINVAL;
    if (len == 0)
        return LHT_EAGAIN;

    const char *p = buf;
    const char *end = buf + len;

    /* method: up to first SP */
    const char *sp = memchr(p, ' ', (size_t)(end - p));
    if (!sp)
        return LHT_EPARSE;
    *method = p;
    *method_len = (size_t)(sp - p);
    p = sp + 1;

    /* path: up to next SP */
    sp = memchr(p, ' ', (size_t)(end - p));
    if (!sp)
        return LHT_EPARSE;
    *path = p;
    *path_len = (size_t)(sp - p);
    p = sp + 1;

    /* HTTP/x.y */
    if (end - p < 10 || memcmp(p, "HTTP/", 5) != 0)
        return LHT_EPARSE;
    p += 5;
    if (p[0] < '0' || p[0] > '9' || p[1] != '.' ||
        p[2] < '0' || p[2] > '9')
        return LHT_EPARSE;
    *minor_version = p[2] - '0';
    p += 3;

    /* expect CRLF (or bare LF in tolerant mode - we accept both) */
    if (p < end && *p == '\r') ++p;
    if (p >= end || *p != '\n')
        return LHT_EPARSE;
    ++p;

    size_t start = (size_t)(p - buf);
    int hres = lht_parse_headers(buf, len, start, headers, num_headers);
    return hres;
}

/* ------------------------------------------------------------------ */
/* One-shot response parser                                            */
/* ------------------------------------------------------------------ */

int lht_parse_response(const char *buf, size_t len,
                       int *status_code,
                       const char **reason, size_t *reason_len,
                       int *minor_version,
                       lht_header_t *headers, size_t *num_headers)
{
    if (!buf || !status_code || !reason || !reason_len ||
        !minor_version || !headers || !num_headers)
        return LHT_EINVAL;
    if (len == 0)
        return LHT_EAGAIN;

    const char *p = buf;
    const char *end = buf + len;

    if (end - p < 12 || memcmp(p, "HTTP/", 5) != 0)
        return LHT_EPARSE;
    p += 5;
    if (p[0] < '0' || p[0] > '9' || p[1] != '.' ||
        p[2] < '0' || p[2] > '9')
        return LHT_EPARSE;
    *minor_version = p[2] - '0';
    p += 3;
    if (*p != ' ')
        return LHT_EPARSE;
    ++p;

    /* status code: exactly 3 digits */
    if (end - p < 4 || p[0] < '0' || p[0] > '9' ||
        p[1] < '0' || p[1] > '9' || p[2] < '0' || p[2] > '9')
        return LHT_EPARSE;
    *status_code = (p[0] - '0') * 100 + (p[1] - '0') * 10 + (p[2] - '0');
    p += 3;

    /* optional SP + reason phrase */
    if (*p == ' ')
        ++p;
    const char *nl = find_nl(p, (size_t)(end - p));
    if (nl >= end)
        return LHT_EPARSE;
    *reason = p;
    *reason_len = (size_t)(nl - p);
    /* trim trailing OWS/CR */
    while (*reason_len > 0 &&
           ((*reason)[*reason_len - 1] == ' ' ||
            (*reason)[*reason_len - 1] == '\t' ||
            (*reason)[*reason_len - 1] == '\r'))
        --*reason_len;

    p = nl;
    if (p < end && *p == '\r') ++p;
    if (p >= end || *p != '\n')
        return LHT_EPARSE;
    ++p;

    size_t start = (size_t)(p - buf);
    return lht_parse_headers(buf, len, start, headers, num_headers);
}

/* ------------------------------------------------------------------ */
/* Streaming parser                                                    */
/* ------------------------------------------------------------------ */

void lht_stream_init(lht_stream_t *s, lht_msg_type_t type)
{
    memset(s, 0, sizeof(*s));
    s->type = (uint8_t)type;
    s->state = s_start;
    s->content_length = ~0ull;
    s->max_headers = 1024;
    s->max_header_size = 8192;
}

void lht_stream_reset(lht_stream_t *s)
{
    lht_msg_type_t t = (lht_msg_type_t)s->type;
    size_t mh = s->max_headers;
    size_t ms = s->max_header_size;
    void *data = s->data;
    memset(s, 0, sizeof(*s));
    s->type = (uint8_t)t;
    s->state = s_start;
    s->content_length = ~0ull;
    s->max_headers = mh;
    s->max_header_size = ms;
    s->data = data;
}

static int stream_error(lht_stream_t *s) { s->state = (uint32_t)-1; return LHT_EPARSE; }

int lht_stream_error(const lht_stream_t *s)
{
    return (s->state == (uint32_t)-1) ? LHT_EPARSE : LHT_OK;
}

size_t lht_stream_execute(lht_stream_t *s, const lht_settings_t *settings,
                          const char *data, size_t len)
{
    const char *p = data;
    const char *end = data + len;
    if (settings && settings->data)
        s->data = settings->data;

    while (p < end) {
        char c = *p;
        switch (s->state) {
        case s_start:
            s->state = (s->type == LHT_MSG_REQUEST) ? s_req_method : s_res_http_h;
            continue;

        /* ---- request line ---- */
        case s_req_method: {
            const char *start = p;
            while (p < end && *p != ' ' && *p != '\r' && *p != '\n' &&
                   (unsigned char)*p > 32) ++p;
            if (p == end) return p - data;
            if (*p != ' ')
                return stream_error(s), p - data;
            size_t n = (size_t)(p - start);
            if (n > 0)
                s->method = (uint8_t)method_index(start, n);
            p++; /* skip SP */
            s->state = s_req_path;
            continue;
        }
        case s_req_path: {
            const char *start = p;
            while (p < end && *p != ' ') ++p;
            if (p == end) {
                size_t n = (size_t)(p - start);
                if (n && settings && settings->on_url)
                    if (settings->on_url(s, start, n) != 0) return p - data;
                return p - data;
            }
            size_t n = (size_t)(p - start);
            if (settings && settings->on_url)
                if (settings->on_url(s, start, n) != 0) return p - data;
            p++;
            s->state = s_req_http_h;
            continue;
        }
        case s_req_http_h:
            if (c != 'H') return stream_error(s), p - data;
            s->state = s_req_http_ht; ++p; continue;
        case s_req_http_ht:
            if (c != 'T') return stream_error(s), p - data;
            s->state = s_req_http_htt; ++p; continue;
        case s_req_http_htt:
            if (c != 'T') return stream_error(s), p - data;
            s->state = s_req_http_http; ++p; continue;
        case s_req_http_http:
            if (c != 'P') return stream_error(s), p - data;
            s->state = s_req_http_slash; ++p; continue;
        case s_req_http_slash:
            if (c != '/') return stream_error(s), p - data;
            s->state = s_req_http_major; ++p; continue;
        case s_req_http_major:
            if (c < '0' || c > '9') return stream_error(s), p - data;
            s->http_major = (uint8_t)(c - '0');
            s->state = s_req_http_dot; ++p; continue;
        case s_req_http_dot:
            if (c != '.') return stream_error(s), p - data;
            s->state = s_req_http_minor; ++p; continue;
        case s_req_http_minor:
            if (c < '0' || c > '9') return stream_error(s), p - data;
            s->http_minor = (uint8_t)(c - '0');
            s->state = s_req_line_cr; ++p; continue;
        case s_req_line_cr:
            if (c == '\r') { s->state = s_req_line_lf; ++p; continue; }
            if (c == '\n') { s->state = s_header_field_start; ++p; continue; }
            return stream_error(s), p - data;
        case s_req_line_lf:
            if (c != '\n') return stream_error(s), p - data;
            s->state = s_header_field_start; ++p; continue;

        /* ---- response line ---- */
        case s_res_http_h:
            if (c != 'H') return stream_error(s), p - data;
            s->state = s_res_http_ht; ++p; continue;
        case s_res_http_ht:
            if (c != 'T') return stream_error(s), p - data;
            s->state = s_res_http_htt; ++p; continue;
        case s_res_http_htt:
            if (c != 'T') return stream_error(s), p - data;
            s->state = s_res_http_http; ++p; continue;
        case s_res_http_http:
            if (c != 'P') return stream_error(s), p - data;
            s->state = s_res_http_slash; ++p; continue;
        case s_res_http_slash:
            if (c != '/') return stream_error(s), p - data;
            s->state = s_res_http_major; ++p; continue;
        case s_res_http_major:
            if (c < '0' || c > '9') return stream_error(s), p - data;
            s->http_major = (uint8_t)(c - '0');
            s->state = s_res_http_dot; ++p; continue;
        case s_res_http_dot:
            if (c != '.') return stream_error(s), p - data;
            s->state = s_res_http_minor; ++p; continue;
        case s_res_http_minor:
            if (c < '0' || c > '9') return stream_error(s), p - data;
            s->http_minor = (uint8_t)(c - '0');
            s->state = s_res_status_sp; ++p; continue;
        case s_res_status_sp:
            if (c != ' ') return stream_error(s), p - data;
            s->state = s_res_status; ++p; continue;
        case s_res_status: {
            const char *start = p;
            int code = 0;
            int digits = 0;
            while (p < end && *p >= '0' && *p <= '9' && digits < 3) {
                code = code * 10 + (*p - '0');
                ++p;
                ++digits;
            }
            if (p == start) return stream_error(s), p - data;
            /* reject if there are more than 3 digits (RFC 7230: exactly 3) */
            if (p < end && *p >= '0' && *p <= '9')
                return stream_error(s), p - data;
            s->status_code = (uint16_t)code;
            if (p < end && *p == ' ') {
                p++;
                s->state = s_res_reason;
            } else {
                s->state = s_res_line_cr;
            }
            continue;
        }
        case s_res_reason: {
            const char *start = p;
            while (p < end && *p != '\r' && *p != '\n') ++p;
            if (p == end) return p - data;
            size_t n = (size_t)(p - start);
            if (settings && settings->on_status)
                if (settings->on_status(s, start, n) != 0) return p - data;
            s->state = s_res_line_cr;
            continue;
        }
        case s_res_line_cr:
            if (c == '\r') { s->state = s_res_line_lf; ++p; continue; }
            if (c == '\n') { s->state = s_header_field_start; ++p; continue; }
            return stream_error(s), p - data;
        case s_res_line_lf:
            if (c != '\n') return stream_error(s), p - data;
            s->state = s_header_field_start; ++p; continue;

        /* ---- header field ---- */
        case s_header_field_start:
            s->cur_field_len = 0;
            s->cur_value_len = 0;
            s->is_te_cl_conn = 0;
            if (c == '\r') { s->state = s_headers_done; ++p; continue; }
            if (c == '\n') {
                s->state = s_headers_done;
                goto headers_complete;
            }
            /* fallthrough */
        case s_header_field: {
            const char *start = p;
            while (p < end && *p != ':' && *p != '\r' && *p != '\n') ++p;
            if (p == end) {
                size_t n = (size_t)(p - start);
                if (s->cur_field_len + n < sizeof(s->cur_field)) {
                    memcpy(s->cur_field + s->cur_field_len, start, n);
                    s->cur_field_len += n;
                }
                s->state = s_header_field; /* don't re-enter s_header_field_start */
                if (settings && settings->on_header_field)
                    if (settings->on_header_field(s, start, n) != 0) return p - data;
                return p - data;
            }
            if (*p != ':') return stream_error(s), p - data;
            size_t n = (size_t)(p - start);
            if (s->cur_field_len + n < sizeof(s->cur_field)) {
                memcpy(s->cur_field + s->cur_field_len, start, n);
                s->cur_field_len += n;
            }
            /* classify the field for built-in hop-by-hop detection */
            if (lht_ascii_ieq(s->cur_field, s->cur_field_len, "Transfer-Encoding", 17) ||
                lht_ascii_ieq(s->cur_field, s->cur_field_len, "Content-Length", 14) ||
                lht_ascii_ieq(s->cur_field, s->cur_field_len, "Connection", 10))
                s->is_te_cl_conn = 1;
            if (settings && settings->on_header_field)
                if (settings->on_header_field(s, start, n) != 0) return p - data;
            s->header_count++;
            if (s->header_count > s->max_headers)
                return stream_error(s), p - data;
            p++; /* skip ':' */
            s->state = s_header_value_discard_ws;
            continue;
        }
        case s_header_value_discard_ws:
            while (p < end && (*p == ' ' || *p == '\t')) ++p;
            s->state = s_header_value_start;
            continue;
        case s_header_value_start:
        case s_header_value: {
            const char *start = p;
            while (p < end && *p != '\r' && *p != '\n') ++p;
            size_t n = (size_t)(p - start);
            /* accumulate value for TE/CL/Conn detection (these are short) */
            if (s->is_te_cl_conn && s->cur_value_len + n < sizeof(s->cur_value)) {
                memcpy(s->cur_value + s->cur_value_len, start, n);
                s->cur_value_len += n;
            }
            if (p == end) {
                s->state = s_header_value; /* don't re-enter s_header_value_start */
                if (settings && settings->on_header_value)
                    if (settings->on_header_value(s, start, n) != 0) return p - data;
                return p - data;
            }
            /* end of value: do hop-by-hop header detection using accumulated buffers */
            if (s->is_te_cl_conn) {
                if (lht_ascii_ieq(s->cur_field, s->cur_field_len, "Transfer-Encoding", 17)) {
                    if (lht_header_has_token(s->cur_value, s->cur_value_len, "chunked", 7))
                        s->is_chunked = 1;
                } else if (lht_ascii_ieq(s->cur_field, s->cur_field_len, "Content-Length", 14)) {
                    uint64_t v = 0;
                    for (size_t i = 0; i < s->cur_value_len; ++i) {
                        if (s->cur_value[i] < '0' || s->cur_value[i] > '9') break;
                        v = v * 10 + (uint64_t)(s->cur_value[i] - '0');
                    }
                    s->content_length = v;
                } else if (lht_ascii_ieq(s->cur_field, s->cur_field_len, "Connection", 10)) {
                    if (lht_header_has_token(s->cur_value, s->cur_value_len, "keep-alive", 10))
                        s->keep_alive = 1;
                    if (lht_header_has_token(s->cur_value, s->cur_value_len, "close", 5))
                        s->keep_alive = 0;
                    if (lht_header_has_token(s->cur_value, s->cur_value_len, "upgrade", 7))
                        s->upgrade = 1;
                }
            }
            if (settings && settings->on_header_value)
                if (settings->on_header_value(s, start, n) != 0) return p - data;
            s->state = s_header_line_cr;
            continue;
        }
        case s_header_line_cr:
            if (c == '\r') { s->state = s_header_line_lf; ++p; continue; }
            if (c == '\n') { s->state = s_header_field_start; ++p; continue; }
            return stream_error(s), p - data;
        case s_header_line_lf:
            if (c != '\n') return stream_error(s), p - data;
            s->state = s_header_field_start; ++p; continue;

        case s_headers_done: {
        headers_complete:
            if (c != '\n') return stream_error(s), p - data;
            if (settings && settings->on_headers_complete)
                if (settings->on_headers_complete(s) != 0) return p - data;
            /* decide body handling */
            int no_body = 0;
            /* requests without CL/TE have no body */
            if (s->type == LHT_MSG_REQUEST && s->content_length == ~0ull && !s->is_chunked)
                no_body = 1;
            /* responses: 1xx, 204, 304 have no body */
            if (s->type == LHT_MSG_RESPONSE &&
                (s->status_code < 200 || s->status_code == 204 || s->status_code == 304))
                no_body = 1;
            /* HEAD requests never have a body */
            if (s->method == LHT_M_HEAD)
                no_body = 1;

            if (no_body) {
                s->state = s_message_done;
                if (settings && settings->on_message_complete)
                    if (settings->on_message_complete(s) != 0) return p - data;
            } else if (s->is_chunked) {
                s->state = s_chunk_size_start;
            } else if (s->content_length != ~0ull) {
                s->state = (s->content_length == 0) ? s_message_done : s_body;
                if (s->state == s_message_done) {
                    if (settings && settings->on_message_complete)
                        if (settings->on_message_complete(s) != 0) return p - data;
                }
            } else {
                s->state = s_body; /* response: read until EOF */
            }
            ++p;
            continue;
        }

        /* ---- body ---- */
        case s_body: {
            size_t avail = (size_t)(end - p);
            size_t take = avail;
            if (s->content_length != ~0ull) {
                uint64_t left = s->content_length;
                take = (avail < (size_t)left) ? avail : (size_t)left;
            }
            if (take > 0 && !(s->flags & LHT_CONF_SKIPBODY)) {
                if (settings && settings->on_body)
                    if (settings->on_body(s, p, take) != 0) return p - data;
            }
            p += take;
            if (s->content_length != ~0ull) {
                s->content_length -= take;
                if (s->content_length == 0) {
                    s->state = s_message_done;
                    if (settings && settings->on_message_complete)
                        if (settings->on_message_complete(s) != 0) return p - data;
                }
            }
            continue;
        }

        /* ---- chunked body ---- */
        case s_chunk_size_start:
        case s_chunk_size: {
            const char *start = p;
            uint64_t sz = s->chunk_remaining;
            while (p < end) {
                int h = lht_hexval((unsigned char)*p);
                if (h < 0) break;
                if (sz > (UINT64_MAX >> 4))
                    return stream_error(s), p - data;
                sz = (sz << 4) | (uint64_t)h;
                ++p;
            }
            if (p == start && s->state == s_chunk_size_start) {
                if (p == end) return p - data;
                return stream_error(s), p - data;
            }
            s->chunk_remaining = sz;
            s->state = s_chunk_parameters;
            continue;
        }
        case s_chunk_parameters:
            while (p < end && *p != '\r' && *p != '\n') ++p;
            if (p == end) return p - data;
            s->state = s_chunk_size_cr;
            continue;
        case s_chunk_size_cr:
            if (c != '\r') return stream_error(s), p - data;
            s->state = s_chunk_size_lf; ++p; continue;
        case s_chunk_size_lf:
            if (c != '\n') return stream_error(s), p - data;
            if (s->chunk_remaining == 0) {
                s->state = s_chunk_trailer_start;  /* consume trailers + final CRLF */
            } else {
                s->state = s_chunk_data;
            }
            ++p; continue;
        case s_chunk_data: {
            size_t avail = (size_t)(end - p);
            size_t take = (avail < (size_t)s->chunk_remaining)
                              ? avail
                              : (size_t)s->chunk_remaining;
            if (take > 0 && !(s->flags & LHT_CONF_SKIPBODY)) {
                if (settings && settings->on_body)
                    if (settings->on_body(s, p, take) != 0) return p - data;
            }
            p += take;
            s->chunk_remaining -= take;
            if (s->chunk_remaining == 0)
                s->state = s_chunk_data_cr;
            continue;
        }
        case s_chunk_data_cr:
            if (c != '\r') return stream_error(s), p - data;
            s->state = s_chunk_data_lf; ++p; continue;
        case s_chunk_data_lf:
            if (c != '\n') return stream_error(s), p - data;
            s->state = s_chunk_size_start; ++p; continue;

        case s_chunk_trailer_start:
            /* consume optional trailer fields + final blank line */
            while (p < end) {
                if (*p == '\r') {
                    ++p;
                    if (p < end && *p == '\n') {
                        ++p;
                        s->state = s_message_done;
                        if (settings && settings->on_message_complete)
                            if (settings->on_message_complete(s) != 0) return p - data;
                        return p - data;
                    }
                } else if (*p == '\n') {
                    ++p;
                    s->state = s_message_done;
                    if (settings && settings->on_message_complete)
                        if (settings->on_message_complete(s) != 0) return p - data;
                    return p - data;
                } else {
                    /* skip trailer line */
                    while (p < end && *p != '\r' && *p != '\n') ++p;
                    if (p == end) return p - data;
                }
            }
            return p - data;

        case s_message_done:
            return p - data;

        default:
            return stream_error(s), p - data;
        }
    }
    return len;
}

int lht_stream_finish(lht_stream_t *s, const lht_settings_t *settings)
{
    switch (s->state) {
    case s_message_done:
        return LHT_OK;
    case s_body:
        if (s->content_length == ~0ull) {  /* read-until-EOF body */
            s->state = s_message_done;
            if (settings && settings->on_message_complete)
                if (settings->on_message_complete(s) != 0)
                    return LHT_EPARSE;
            return LHT_OK;
        }
        return LHT_EPARSE;
    default:
        return LHT_EPARSE;
    }
}

/* ------------------------------------------------------------------ */
/* Header helpers                                                      */
/* ------------------------------------------------------------------ */

int lht_strcasecmp(const char *a, size_t alen, const char *b, size_t blen)
{
    return lht_ascii_ieq(a, alen, b, blen) ? 0 : 1;
}

int lht_find_header(const lht_header_t *headers, size_t count,
                    const char *name, size_t name_len)
{
    for (size_t i = 0; i < count; ++i) {
        if (lht_ascii_ieq(headers[i].name, headers[i].name_len, name, name_len))
            return (int)i;
    }
    return -1;
}

int lht_header_has_token(const char *value, size_t len,
                         const char *token, size_t token_len)
{
    const char *p = value;
    const char *end = value + len;
    while (p < end) {
        while (p < end && (*p == ' ' || *p == ',' || *p == '\t')) ++p;
        const char *tok = p;
        while (p < end && *p != ',' && *p != ' ' && *p != '\t') ++p;
        if ((size_t)(p - tok) == token_len &&
            lht_ascii_ieq(tok, token_len, token, token_len))
            return 1;
    }
    return 0;
}
