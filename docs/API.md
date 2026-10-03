# lightning API reference

Single public header: [`include/lightning/lightning.h`](../include/lightning/lightning.h).
All symbols are C, prefixed `lht_`. C++ users get `extern "C"` automatically.

Return codes are the same across the library:

| Code             | Value | Meaning                                       |
| ---------------- | ----: | -------------------------------------------- |
| `LHT_OK`         |   `0` | Success (or, for one-shot parsers, body offset ≥ 0) |
| `LHT_EAGAIN`     |  `-1` | Need more data (streaming only); or, for one-shot, ran out of bytes before blank line |
| `LHT_EPARSE`     |  `-2` | Malformed input                              |
| `LHT_EOVERFLOW`  |  `-3` | Too many headers or header too large         |
| `LHT_EINVAL`     |  `-4` | Invalid argument (NULL pointer, etc.)        |
| `LHT_ENOSYS`     |  `-5` | Feature not compiled in                      |

For the one-shot parsers, a non-negative return value is the byte offset of
the body inside the input buffer. Pointers returned by all parsers reference
the caller's buffer directly; they are valid only while that buffer remains
alive and unchanged.

---

## One-shot parsers

For full-buffer parsing: proxies, caches, log miners. The whole request or
response head must already be in memory.

### `lht_parse_request`

```c
int lht_parse_request(const char *buf, size_t len,
                      const char **method, size_t *method_len,
                      const char **path, size_t *path_len,
                      int *minor_version,
                      lht_header_t *headers, size_t *num_headers);
```

Parse an HTTP/1.x request head.

| Parameter        | Direction | Meaning                                    |
| ---------------- | --------- | ----------------------------------------- |
| `buf, len`       | in        | Input buffer with full request head       |
| `method`         | out       | Method token, e.g. `"GET"` (zero-copy)     |
| `method_len`     | out       | Length of `method`                        |
| `path`           | out       | Request-target / path (zero-copy)          |
| `path_len`       | out       | Length of `path`                          |
| `minor_version`  | out       | HTTP minor version (`0` for 1.0, `1` for 1.1) |
| `headers`        | out       | Caller-owned `lht_header_t` array, filled in |
| `num_headers`    | in/out    | In: array capacity. Out: parsed count      |

Returns the body offset on success, or a negative `LHT_E*` code. If the
return is `LHT_EOVERFLOW`, `*num_headers` is set to the count that fit; the
caller can grow the array and retry.

```c
const char *req =
    "POST /api/v1/orders HTTP/1.1\r\n"
    "Host: api.example.com\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: 13\r\n"
    "\r\n"
    "{\"x\":1,\"y\":2}";

lht_header_t hdrs[16];
size_t nh = 16;
const char *m, *p; size_t mlen, plen; int minor;
int off = lht_parse_request(req, strlen(req),
                            &m, &mlen, &p, &plen, &minor, hdrs, &nh);
assert(off >= 0);
assert(memcmp(m, "POST", 4) == 0);
assert(memcmp(p, "/api/v1/orders", 14) == 0);
assert(minor == 1);
/* body lives at req + off */
```

### `lht_parse_response`

```c
int lht_parse_response(const char *buf, size_t len,
                       int *status_code,
                       const char **reason, size_t *reason_len,
                       int *minor_version,
                       lht_header_t *headers, size_t *num_headers);
```

Parse an HTTP/1.x response head. Same calling convention as
`lht_parse_request`; outputs are `status_code` (e.g. `200`), `reason`
(e.g. `"OK"`, may be empty), and `minor_version`.

### `lht_parse_headers`

```c
int lht_parse_headers(const char *buf, size_t len, size_t start,
                      lht_header_t *headers, size_t *num_headers);
```

Parse a header block starting at byte offset `start` inside `buf`. Useful
when the request/response line has already been consumed. Returns the offset
just past the terminating blank line, or a negative `LHT_E*` code. `LHT_EAGAIN`
means the buffer ran out before the blank line was seen.

### `lht_header_t`

```c
typedef struct lht_header {
    const char *name;
    size_t      name_len;
    const char *value;
    size_t      value_len;
} lht_header_t;
```

A single parsed header. All four fields reference `buf`. Trailing OWS
(whitespace) is stripped from both `name` and `value`.

---

## Streaming parser

For partial reads off a socket. State is held in `lht_stream_t`; the caller
allocates and owns it. Bytes are fed in via `lht_stream_execute`, which fires
callbacks as tokens are recognized.

### `lht_stream_init` / `lht_stream_reset`

```c
void lht_stream_init(lht_stream_t *s, lht_msg_type_t type);
void lht_stream_reset(lht_stream_t *s);
```

`lht_stream_init` zeroes the struct, sets the message type
(`LHT_MSG_REQUEST` or `LHT_MSG_RESPONSE`), and applies default limits
(`max_headers = 1024`, `max_header_size = 8192`). `lht_stream_reset` clears
parse state but preserves type, limits, and the user `data` pointer — use
it between pipelined messages on the same connection.

### `lht_stream_execute`

```c
size_t lht_stream_execute(lht_stream_t *s, const lht_settings_t *settings,
                          const char *data, size_t len);
```

Feed `len` bytes to the parser. Returns the number of bytes consumed (always
≤ `len`). A return `< len` means the parser stopped — either the message is
complete, an error occurred, or the parser paused waiting for more data.
Check `lht_stream_error(s)` and your own state in `s->data` to decide.

Callbacks (any of which may be `NULL`) are called from inside this function
in the order the bytes are recognized:

```c
typedef struct lht_settings {
    int (*on_message_begin)   (lht_stream_t *s);
    int (*on_url)             (lht_stream_t *s, const char *at, size_t len);
    int (*on_status)          (lht_stream_t *s, const char *at, size_t len);
    int (*on_header_field)    (lht_stream_t *s, const char *at, size_t len);
    int (*on_header_value)    (lht_stream_t *s, const char *at, size_t len);
    int (*on_headers_complete)(lht_stream_t *s);
    int (*on_body)            (lht_stream_t *s, const char *at, size_t len);
    int (*on_message_complete)(lht_stream_t *s);
    void *data;
} lht_settings_t;
```

A callback returning non-zero stops execution; `lht_stream_execute` returns
the offset just past the last consumed byte. `on_url` and `on_status` are
mutually exclusive per message. `on_header_field`/`on_header_value` are
called alternately, once per header. `at` pointers reference the input
buffer; they are valid until the next call to `lht_stream_execute`.

```c
typedef struct { char body[4096]; size_t len; int done; } ctx_t;

static int on_body(lht_stream_t *s, const char *at, size_t len) {
    ctx_t *c = s->data;
    if (c->len + len < sizeof(c->body)) {
        memcpy(c->body + c->len, at, len);
        c->len += len;
    }
    return 0;
}
static int on_mc(lht_stream_t *s) { ((ctx_t*)s->data)->done = 1; return 0; }

lht_stream_t s;
lht_stream_init(&s, LHT_MSG_REQUEST);
ctx_t ctx = {0};
s.data = &ctx;

lht_settings_t st = {0};
st.on_body = on_body;
st.on_message_complete = on_mc;
st.data = &ctx;

/* read() loop, feeding arbitrary chunk sizes */
ssize_t n;
char buf[4096];
while ((n = read(fd, buf, sizeof(buf))) > 0) {
    size_t off = 0;
    while (off < (size_t)n) {
        size_t c = lht_stream_execute(&s, &st, buf + off, (size_t)n - off);
        if (c == 0) break;
        off += c;
    }
    if (ctx.done) break;
    if (lht_stream_error(&s) != LHT_OK) break;
}
```

### `lht_stream_finish`

```c
int lht_stream_finish(lht_stream_t *s, const lht_settings_t *settings);
```

Signal end of input. Returns `LHT_OK` if the message is fully parsed,
`LHT_OK` again for a response in `s_body` with no `Content-Length` (read-
until-EOF), or `LHT_EPARSE` for truncation. Call after the last
`lht_stream_execute` to flush state for responses with no body framing.

### `lht_stream_error`

```c
int lht_stream_error(const lht_stream_t *s);
```

Returns `LHT_EPARSE` if the parser hit a malformed byte, else `LHT_OK`. Once
errored, the parser is parked; call `lht_stream_reset` to reuse the struct.

### Stream fields (read-only after `on_headers_complete`)

| Field             | Type     | Meaning                                       |
| ----------------- | -------- | --------------------------------------------- |
| `http_major`      | `uint8_t`| HTTP major version (always 1)                 |
| `http_minor`      | `uint8_t`| HTTP minor version (0 or 1)                   |
| `status_code`     | `uint16_t`| Response status code (responses only)       |
| `method`          | `uint8_t`| Method index, see `lht_method_str` (0 = custom) |
| `keep_alive`      | `uint8_t`| Connection keep-alive hint (1 = keep-alive)  |
| `content_length`  | `uint64_t`| Parsed `Content-Length`, or `~0ull` if absent |
| `is_chunked`      | `uint8_t`| 1 if `Transfer-Encoding: chunked` seen        |
| `upgrade`         | `uint8_t`| 1 if `Connection: upgrade` seen               |

### `lht_method_str` / `lht_method_match`

```c
const char *lht_method_str(int method);
int lht_method_match(const lht_stream_t *s, const char *m, size_t mlen);
```

`lht_method_str` returns the canonical name for a method index
(`"GET"`, `"POST"`, `"DELETE"`, …), or `NULL` for unknown/custom methods.
`lht_method_match` is a convenience: case-insensitive compare of `s->method`
against `m`.

### Configuration flags

```c
typedef enum {
    LHT_CONF_NORMAL   = 0x00,
    LHT_CONF_TOLERANT = 0x01,  /* accept LF without CR, spaces around colon */
    LHT_CONF_SKIPBODY = 0x02,  /* do not deliver body chunks via on_body   */
} lht_conf_flags_t;
```

Set `s->flags` after `lht_stream_init` and before the first
`lht_stream_execute`. `LHT_CONF_TOLERANT` matches what real-world servers
accept; `LHT_CONF_SKIPBODY` is for proxies that only care about headers.

---

## Chunked transfer-encoding decoder

Standalone incremental decoder for `Transfer-Encoding: chunked`. Drives off
a small state struct; can be fed one byte at a time without breaking.

```c
typedef struct lht_chunked {
    uint64_t size;          /* current chunk size          */
    uint64_t remaining;     /* bytes left in current chunk */
    int      state;
    int      done;
} lht_chunked_t;

void lht_chunked_init(lht_chunked_t *c);
size_t lht_chunked_execute(lht_chunked_t *c, const char *in, size_t in_len,
                           const char **out, size_t *out_len);
```

`lht_chunked_execute` returns the number of input bytes consumed. On a
complete chunk, `*out` and `*out_len` point at the chunk data inside `in`
(zero-copy). When the terminating zero-length chunk is seen, `c->done` is
set to 1. Returns `(size_t)-1` on malformed input.

```c
const char *in = "5\r\nHello\r\n6\r\n World\r\n0\r\n\r\n";
lht_chunked_t c;
lht_chunked_init(&c);

char out[64]; size_t off = 0;
const char *p = in;
size_t remain = strlen(in);
while (remain > 0 && !c.done) {
    const char *o; size_t olen;
    size_t consumed = lht_chunked_execute(&c, p, remain, &o, &olen);
    if (consumed == (size_t)-1) { /* parse error */ break; }
    if (olen) { memcpy(out + off, o, olen); off += olen; }
    p += consumed;
    remain -= consumed;
}
/* out[0..off] == "Hello World" */
```

The streaming parser already handles chunked bodies inline; use the
standalone decoder when you only need to decode a chunked stream outside the
full HTTP parser (e.g. proxying to a non-HTTP sink).

---

## URL parser

RFC 3986 splitter. All fields point into the input `url`.

```c
typedef struct lht_url {
    const char *scheme;    size_t scheme_len;
    const char *userinfo;  size_t userinfo_len;
    const char *host;      size_t host_len;
    const char *port;      size_t port_len;
    const char *path;      size_t path_len;
    const char *query;     size_t query_len;
    const char *fragment;  size_t fragment_len;
} lht_url_t;

int lht_parse_url(const char *url, size_t len, lht_url_t *out, int is_connect);
```

Accepts absolute (`https://user:pw@example.com:8080/p?q=1#x`), origin-form
(`/path?q=1`), and authority-form (`example.com:443` when `is_connect != 0`,
for `CONNECT` requests). IPv6 literals (`[::1]`) are parsed correctly.
Returns `LHT_OK` or `LHT_EPARSE`.

```c
lht_url_t u;
lht_parse_url("https://user:pw@example.com:8080/p?q=1#x", 40, &u, 0);
/* u.scheme   = "https"       u.host = "example.com"
   u.userinfo = "user:pw"     u.port = "8080"
   u.path     = "/p"          u.query = "q=1"  u.fragment = "x" */
```

### `lht_percent_decode`

```c
size_t lht_percent_decode(const char *in, size_t len, char *out);
```

Percent-decode `in` into `out`. `out` must hold at least `len` bytes.
`%XX` sequences are decoded; `+` is treated as space (form-compatible).
Malformed `%XX` (e.g. `%ZZ`) is copied verbatim. Returns the decoded length.

### `lht_parse_query`

```c
typedef struct lht_kv {
    const char *key;   size_t key_len;
    const char *value; size_t value_len;
} lht_kv_t;

size_t lht_parse_query(const char *query, size_t len,
                       lht_kv_t *pairs, size_t max_pairs);
```

Split a query string on `&`, then on `=`. Pairs without `=` get
`value = NULL, value_len = 0`. Empty values (`key=`) get `value_len = 0` with
a non-NULL pointer. Returns the number of pairs found (capped at
`max_pairs`).

```c
lht_kv_t kv[8];
size_t n = lht_parse_query("a=1&b=2&c=&d", 13, kv, 8);
/* n == 4
   kv[0] = {"a","1"}   kv[1] = {"b","2"}
   kv[2] = {"c",""}    kv[3] = {"d", NULL} */
```

---

## Header helpers

### `lht_find_header`

```c
int lht_find_header(const lht_header_t *headers, size_t count,
                    const char *name, size_t name_len);
```

Case-insensitive linear scan. Returns the index of the first match, or `-1`.
HTTP header names are case-insensitive per RFC 7230 §3.2.

```c
int idx = lht_find_header(hdrs, nh, "content-type", 12);
if (idx >= 0)
    printf("Content-Type: %.*s\n",
           (int)hdrs[idx].value_len, hdrs[idx].value);
```

### `lht_header_has_token`

```c
int lht_header_has_token(const char *value, size_t len,
                         const char *token, size_t token_len);
```

Test whether a comma-separated header value contains `token`.
Case-insensitive, ignores whitespace. Useful for `Connection`,
`Transfer-Encoding`, `Cache-Control`, `Accept-*`.

```c
const char *v = "keep-alive, 100-continue";
lht_header_has_token(v, strlen(v), "100-continue", 12);  /* 1 */
lht_header_has_token(v, strlen(v), "close", 5);          /* 0 */
```

### `lht_strcasecmp`

```c
int lht_strcasecmp(const char *a, size_t alen, const char *b, size_t blen);
```

ASCII case-insensitive compare. Returns 0 on equality (matching
`strcasecmp` semantics), non-zero otherwise. Only ASCII letters are folded.

---

## Utility

### `lht_has_sse42` / `lht_has_avx2` / `lht_simd_backend`

```c
int lht_has_sse42(void);
int lht_has_avx2(void);
const char *lht_simd_backend(void);  /* "avx2" | "sse42" | "scalar" */
```

Runtime CPU feature detection. Cached on first call. `lht_simd_backend`
returns the name of the active SIMD path for the one-shot parser; useful for
logging in benchmarks.

```c
printf("SIMD backend: %s\n", lht_simd_backend());
/* SIMD backend: avx2 */
```
