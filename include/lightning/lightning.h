/*
 * lightning - an extremely fast, zero-copy, zero-allocation HTTP/1.1 parser.
 *
 * Core design principles:
 *   1. Zero allocation on the hot path. Callers own all memory.
 *   2. Zero copy: returned pointers reference the input buffer directly.
 *   3. SIMD accelerated header-line scanning (SSE4.2 / AVX2, runtime-detected).
 *   4. Both one-shot (full buffer) and streaming (partial reads) APIs.
 *   5. Strict RFC 7230 compliance with a tolerant fallback mode.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTNING_H
#define LIGHTNING_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LHT_VERSION_MAJOR 0
#define LHT_VERSION_MINOR 1
#define LHT_VERSION_PATCH 0
#define LHT_VERSION_STRING "0.1.0"

/* Return codes. Negative values are errors; >=0 is the byte offset of the
 * body (for one-shot parsers) or a positive status. */
#define LHT_OK          0
#define LHT_EAGAIN     -1   /* need more data (streaming only) */
#define LHT_EPARSE     -2   /* malformed input */
#define LHT_EOVERFLOW  -3   /* too many headers / header too large */
#define LHT_EINVAL     -4   /* invalid argument */
#define LHT_ENOSYS     -5   /* feature not compiled in */

/* A parsed header: name/value point directly into the caller's buffer. */
typedef struct lht_header {
    const char *name;
    size_t      name_len;
    const char *value;
    size_t      value_len;
} lht_header_t;

/* ------------------------------------------------------------------ */
/* One-shot parsers (full headers already in a single buffer)         */
/* ------------------------------------------------------------------ */

/* Parse an HTTP/1.x request.
 *
 *   buf, len          : input buffer containing the full request head
 *                       (request line + headers, possibly followed by body)
 *   method, method_len: returned method token (e.g. "GET"), zero-copy
 *   path, path_len    : returned target / request-target, zero-copy
 *   minor_version     : HTTP minor version (0 or 1)
 *   headers           : caller-owned array of lht_header_t
 *   num_headers       : in: capacity of headers array; out: parsed count
 *
 * Returns the byte offset of the start of the body on success, or a
 * negative LHT_E* code on failure. The returned pointers are valid only
 * while `buf` remains alive and unchanged.
 */
int lht_parse_request(const char *buf, size_t len,
                      const char **method, size_t *method_len,
                      const char **path, size_t *path_len,
                      int *minor_version,
                      lht_header_t *headers, size_t *num_headers);

/* Parse an HTTP/1.x response. Same semantics as lht_parse_request. */
int lht_parse_response(const char *buf, size_t len,
                       int *status_code,
                       const char **reason, size_t *reason_len,
                       int *minor_version,
                       lht_header_t *headers, size_t *num_headers);

/* Parse only the header block, starting at byte `start` inside buf.
 * `start` should point at the first byte of the first header field.
 * Returns offset just past the terminating blank line, or negative error. */
int lht_parse_headers(const char *buf, size_t len, size_t start,
                      lht_header_t *headers, size_t *num_headers);

/* ------------------------------------------------------------------ */
/* Streaming / incremental parser                                     */
/* ------------------------------------------------------------------ */

typedef enum {
    LHT_MSG_REQUEST  = 1,
    LHT_MSG_RESPONSE = 2
} lht_msg_type_t;

typedef enum {
    LHT_CONF_NORMAL   = 0x00,  /* RFC-strict-ish, default */
    LHT_CONF_TOLERANT = 0x01,  /* accept LF without CR, spaces around colon */
    LHT_CONF_SKIPBODY = 0x02   /* do not deliver body chunks via on_body */
} lht_conf_flags_t;

typedef struct lht_stream lht_stream_t;

typedef struct lht_settings {
    int (*on_message_begin)(lht_stream_t *s);
    int (*on_url)(lht_stream_t *s, const char *at, size_t len);
    int (*on_status)(lht_stream_t *s, const char *at, size_t len);
    int (*on_header_field)(lht_stream_t *s, const char *at, size_t len);
    int (*on_header_value)(lht_stream_t *s, const char *at, size_t len);
    int (*on_headers_complete)(lht_stream_t *s);
    int (*on_body)(lht_stream_t *s, const char *at, size_t len);
    int (*on_message_complete)(lht_stream_t *s);
    void *data;
} lht_settings_t;

struct lht_stream {
    /* public, read-mostly */
    uint8_t       type;            /* lht_msg_type_t */
    uint8_t       flags;           /* lht_conf_flags_t */
    uint8_t       http_major;
    uint8_t       http_minor;
    uint16_t      status_code;     /* responses */
    uint8_t       method;          /* request method index, 0 = custom */
    uint8_t       keep_alive;      /* connection keep-alive hint */
    uint64_t      content_length;  /* parsed Content-Length, or ~0ull */
    uint8_t       is_chunked;
    uint8_t       upgrade;
    void         *data;            /* user pointer, copied from settings */

    /* private state */
    uint32_t      state;
    uint32_t      sub_state;
    uint64_t      chunk_remaining;
    size_t        header_count;
    size_t        consumed;
    /* accumulated current header field/value for cross-boundary tokens */
    char          cur_field[128];
    size_t        cur_field_len;
    char          cur_value[256];
    size_t        cur_value_len;
    uint8_t       is_te_cl_conn;   /* current field is TE/CL/Connection */
    /* limits */
    size_t        max_headers;
    size_t        max_header_size;
};

void lht_stream_init(lht_stream_t *s, lht_msg_type_t type);
void lht_stream_reset(lht_stream_t *s);

/* Feed bytes to the parser. Returns the number of bytes consumed.
 * A return value < len means the parser stopped (error or message complete);
 * check lht_stream_error(). Callbacks fire as tokens are recognized. */
size_t lht_stream_execute(lht_stream_t *s, const lht_settings_t *settings,
                          const char *data, size_t len);

/* Signal end of input. Returns LHT_OK if the message is fully parsed,
 * LHT_EAGAIN if more data was expected, LHT_EPARSE on truncation. */
int lht_stream_finish(lht_stream_t *s, const lht_settings_t *settings);

int  lht_stream_error(const lht_stream_t *s);

/* Method name helper. Returns NULL for unknown/custom methods. */
const char *lht_method_str(int method);
int lht_method_match(const lht_stream_t *s, const char *m, size_t mlen);

/* ------------------------------------------------------------------ */
/* Chunked transfer-encoding decoder                                  */
/* ------------------------------------------------------------------ */

/* Incrementally decode a chunked body.
 *
 *   in, in_len  : new input bytes
 *   out, out_len: on return, points to decoded bytes inside `in` (zero-copy)
 *                 when a complete chunk is available; otherwise *out_len = 0
 *   done        : set to 1 when the final zero-length chunk is seen
 *
 * Returns the number of input bytes consumed. Caller must re-feed the
 * remainder. State must be zero-initialized before first call. */
typedef struct lht_chunked {
    uint64_t size;          /* current chunk size */
    uint64_t remaining;     /* bytes left in current chunk data */
    int      state;
    int      done;
} lht_chunked_t;

void lht_chunked_init(lht_chunked_t *c);
size_t lht_chunked_execute(lht_chunked_t *c, const char *in, size_t in_len,
                           const char **out, size_t *out_len);

/* ------------------------------------------------------------------ */
/* URL parser (RFC 3986)                                               */
/* ------------------------------------------------------------------ */

typedef struct lht_url {
    const char *scheme;    size_t scheme_len;
    const char *userinfo;  size_t userinfo_len;
    const char *host;      size_t host_len;
    const char *port;      size_t port_len;
    const char *path;      size_t path_len;
    const char *query;     size_t query_len;
    const char *fragment;  size_t fragment_len;
} lht_url_t;

/* Parse an absolute or origin-form URL. All fields point into `url`.
 * is_connect: set non-zero for CONNECT requests (authority form).
 * Returns LHT_OK or LHT_EPARSE. */
int lht_parse_url(const char *url, size_t len, lht_url_t *out, int is_connect);

/* Percent-decode `in` into `out`. `out` must be at least `len` bytes.
 * Returns the decoded length. Malformed %XX sequences are copied verbatim. */
size_t lht_percent_decode(const char *in, size_t len, char *out);

/* Split a query string into key/value pairs. Returns number of pairs found
 * (up to max_pairs). Pairs point into `query`. */
typedef struct lht_kv {
    const char *key;   size_t key_len;
    const char *value; size_t value_len;
} lht_kv_t;

size_t lht_parse_query(const char *query, size_t len,
                       lht_kv_t *pairs, size_t max_pairs);

/* ------------------------------------------------------------------ */
/* Header value helpers (zero-copy)                                    */
/* ------------------------------------------------------------------ */

/* Case-insensitive header search. Returns index or -1. */
int lht_find_header(const lht_header_t *headers, size_t count,
                    const char *name, size_t name_len);

/* Parse "100-continue", "chunked", "keep-alive" tokens from a header value.
 * Returns 1 if the named token is present (comma-separated list). */
int lht_header_has_token(const char *value, size_t len,
                         const char *token, size_t token_len);

/* ------------------------------------------------------------------ */
/* Utility                                                             */
/* ------------------------------------------------------------------ */

/* Runtime CPU feature detection. */
int lht_has_sse42(void);
int lht_has_avx2(void);
const char *lht_simd_backend(void); /* "avx2" | "sse42" | "scalar" */

/* Case-insensitive compare of length-bounded strings. */
int lht_strcasecmp(const char *a, size_t alen, const char *b, size_t blen);

#ifdef __cplusplus
}
#endif

#endif /* LIGHTNING_H */
