# lightning API 中文参考

本文档列出 lightning 的全部公开函数签名、语义和简短示例。所有声明集中在
单一头文件 [`include/lightning/lightning.h`](../include/lightning/lightning.h)
中，C99 兼容，C++ 可通过 `extern "C"` 调用。

## 目录

- [返回码与公共类型](#返回码与公共类型)
- [一次性解析器](#一次性解析器)
  - [`lht_parse_request`](#lht_parse_request)
  - [`lht_parse_response`](#lht_parse_response)
  - [`lht_parse_headers`](#lht_parse_headers)
- [流式解析器](#流式解析器)
  - [`lht_stream_init`](#lht_stream_init)
  - [`lht_stream_execute`](#lht_stream_execute)
  - [`lht_stream_finish`](#lht_stream_finish)
  - [`lht_stream_reset`](#lht_stream_reset)
  - [`lht_stream_error`](#lht_stream_error)
  - [`lht_method_str` / `lht_method_match`](#lht_method_str--lht_method_match)
- [chunked 解码器](#chunked-解码器)
  - [`lht_chunked_init`](#lht_chunked_init)
  - [`lht_chunked_execute`](#lht_chunked_execute)
- [URL 解析器](#url-解析器)
  - [`lht_parse_url`](#lht_parse_url)
  - [`lht_percent_decode`](#lht_percent_decode)
  - [`lht_parse_query`](#lht_parse_query)
- [头部辅助函数](#头部辅助函数)
  - [`lht_find_header`](#lht_find_header)
  - [`lht_header_has_token`](#lht_header_has_token)
  - [`lht_strcasecmp`](#lht_strcasecmp)
- [SIMD / 运行时](#simd--运行时)
  - [`lht_has_sse42` / `lht_has_avx2` / `lht_simd_backend`](#lht_has_sse42--lht_has_avx2--lht_simd_backend)

---

## 返回码与公共类型

```c
#define LHT_OK          0
#define LHT_EAGAIN     -1   /* 流式专用：还需要更多数据 */
#define LHT_EPARSE     -2   /* 输入格式错误 */
#define LHT_EOVERFLOW  -3   /* 头太多 / 头太大 */
#define LHT_EINVAL     -4   /* 参数非法 */
#define LHT_ENOSYS     -5   /* 该特性未编译进来 */
```

一次性解析器返回 `int`：`>=0` 是消息体在缓冲区里的字节偏移，`<0` 是上述错误码。
流式解析器中 `lht_stream_execute` 返回本次消费的字节数；状态查询走
`lht_stream_error`。

```c
typedef struct lht_header {
    const char *name;
    size_t      name_len;
    const char *value;
    size_t      value_len;
} lht_header_t;
```

一对 `(ptr, len)` 描述一个头部字段。`name` / `value` 都直接指向调用方传入的
缓冲区，没有拷贝。缓冲区释放或修改后这些指针即失效。

```c
typedef enum {
    LHT_MSG_REQUEST  = 1,
    LHT_MSG_RESPONSE = 2
} lht_msg_type_t;

typedef enum {
    LHT_CONF_NORMAL   = 0x00,  /* 默认，接近 RFC 严格 */
    LHT_CONF_TOLERANT = 0x01,  /* 接受裸 LF、冒号两侧的 OWS */
    LHT_CONF_SKIPBODY = 0x02   /* 不通过 on_body 投递 body 块 */
} lht_conf_flags_t;
```

---

## 一次性解析器

适用于完整头已经在一个缓冲区里的场景：代理、缓存、日志解析。返回的指针指向入参
`buf`，调用方需保证 `buf` 在使用期间存活。

### `lht_parse_request`

```c
int lht_parse_request(const char *buf, size_t len,
                      const char **method, size_t *method_len,
                      const char **path, size_t *path_len,
                      int *minor_version,
                      lht_header_t *headers, size_t *num_headers);
```

**参数**

- `buf, len`：输入缓冲区，包含完整请求头（请求行 + 头部，可以带后续 body）。
- `method, method_len`：出参，方法 token（如 `"GET"`），零拷贝。
- `path, path_len`：出参，request-target，零拷贝。
- `minor_version`：出参，HTTP 次版本号（0 或 1）。
- `headers`：调用方拥有的 `lht_header_t` 数组。
- `num_headers`：入参是数组容量，出参是解析得到的数量。

**返回值**

`>=0`：body 起始偏移。`<0`：`LHT_E*` 错误码。`len == 0` 时返回 `LHT_EAGAIN`。

**示例**

```c
const char *req = "GET /hello HTTP/1.1\r\nHost: example.com\r\n\r\n";
lht_header_t hdrs[16];
size_t nh = 16;
const char *method, *path;
size_t mlen, plen;
int minor;

int body_off = lht_parse_request(req, strlen(req),
                                 &method, &mlen, &path, &plen,
                                 &minor, hdrs, &nh);
if (body_off < 0) { /* 处理错误 */ }

printf("%.*s %.*s HTTP/1.%d\n",
       (int)mlen, method, (int)plen, path, minor);

int idx = lht_find_header(hdrs, nh, "host", 4);
if (idx >= 0)
    printf("Host: %.*s\n", (int)hdrs[idx].value_len, hdrs[idx].value);
```

### `lht_parse_response`

```c
int lht_parse_response(const char *buf, size_t len,
                       int *status_code,
                       const char **reason, size_t *reason_len,
                       int *minor_version,
                       lht_header_t *headers, size_t *num_headers);
```

语义同 `lht_parse_request`，只是给响应用。`status_code` 是三位状态码，
`reason` 是 reason-phrase（已去掉尾部 OWS 和 CR）。

**示例**

```c
const char *resp =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 13\r\n"
    "\r\n"
    "Hello, World!";

int status, minor;
const char *reason;
size_t rlen;
lht_header_t hdrs[8];
size_t nh = 8;

int body_off = lht_parse_response(resp, strlen(resp),
                                  &status, &reason, &rlen,
                                  &minor, hdrs, &nh);
/* body_off 指向 "Hello, World!" 的起始位置 */
```

### `lht_parse_headers`

```c
int lht_parse_headers(const char *buf, size_t len, size_t start,
                      lht_header_t *headers, size_t *num_headers);
```

只解析头部块，从 `buf + start` 处的第一个头字段开始。返回终止空行之后的偏移，
或负数错误码。如果你只想复用 header 解析逻辑而自己处理起始行，用这个。

---

## 流式解析器

适用于从 socket 读到的部分数据，分块边界任意。基于状态机，回调驱动。

### `lht_stream_init`

```c
void lht_stream_init(lht_stream_t *s, lht_msg_type_t type);
```

把 `lht_stream_t` 清零并设置初始状态。`type` 是 `LHT_MSG_REQUEST` 或
`LHT_MSG_RESPONSE`。`s` 必须由调用方拥有（栈或堆都行）。

### `lht_stream_execute`

```c
size_t lht_stream_execute(lht_stream_t *s, const lht_settings_t *settings,
                          const char *data, size_t len);
```

喂字节给解析器。返回本次消费的字节数。返回值 `< len` 表示解析器停了
（出错或消息完成），查 `lht_stream_error`。回调在 token 被识别时触发。

`lht_settings_t` 的回调集如下，全部可选（不设即跳过）：

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

回调返回 0 继续；返回非 0 让 `lht_stream_execute` 立即返回当前消费数。跨数据
块边界的字段名 / 值会通过 `s->cur_field` / `s->cur_value` 累积，便于在
`on_header_field` / `on_header_value` 里拿到完整 token。

`lht_stream_t` 的公开字段（解析完头部后可直接读）：

```c
struct lht_stream {
    uint8_t  type;            /* LHT_MSG_REQUEST / RESPONSE */
    uint8_t  flags;           /* LHT_CONF_* */
    uint8_t  http_major;
    uint8_t  http_minor;
    uint16_t status_code;     /* 响应 */
    uint8_t  method;          /* 请求方法索引，0 = 自定义 */
    uint8_t  keep_alive;
    uint64_t content_length;  /* 解析出的 Content-Length，或 ~0ull */
    uint8_t  is_chunked;
    uint8_t  upgrade;
    void    *data;            /* 用户指针，从 settings 拷贝 */
    /* ... 私有状态 ... */
};
```

### `lht_stream_finish`

```c
int lht_stream_finish(lht_stream_t *s, const lht_settings_t *settings);
```

通知输入结束。状态机处于 `s_message_done` 时返回 `LHT_OK`；处于 `s_body` 且
`content_length` 未定（read-until-EOF）也返回 `LHT_OK`；其他情况返回
`LHT_EPARSE` 表示截断。

### `lht_stream_reset`

```c
void lht_stream_reset(lht_stream_t *s);
```

清空状态但保留 `type`、`max_headers`、`max_header_size` 和 `data`。用于
keep-alive 连接复用同一个解析器对象，省掉 `init` 的全量清零。

### `lht_stream_error`

```c
int lht_stream_error(const lht_stream_t *s);
```

返回 `LHT_OK` 或 `LHT_EPARSE`。出错后状态机卡在错误态，需要 `reset` 才能复用。

### `lht_method_str` / `lht_method_match`

```c
const char *lht_method_str(int method);
int lht_method_match(const lht_stream_t *s, const char *m, size_t mlen);
```

`lht_method_str` 把内部方法索引转成大写名字串（`"GET"`、`"POST"` 等），
未知 / 自定义方法返回 `NULL`。`lht_method_match` 是不区分大小写比较当前流
的方法与给定串。

**完整示例：模拟慢 socket 一次喂 4 字节**

```c
lht_settings_t settings;
memset(&settings, 0, sizeof(settings));
settings.on_url = on_url;
settings.on_body = on_body;
settings.on_headers_complete = on_hc;
settings.on_message_complete = on_mc;

lht_stream_t parser;
lht_stream_init(&parser, LHT_MSG_REQUEST);
ctx_t ctx = {0};
parser.data = &ctx;

const char *full = "POST /upload HTTP/1.1\r\nContent-Length: 11\r\n\r\nhello world";
size_t len = strlen(full), total = 0;
while (total < len) {
    size_t chunk = (len - total < 4) ? (len - total) : 4;
    size_t n = lht_stream_execute(&parser, &settings, full + total, chunk);
    total += n;
    if (n == 0) break;
}
/* ctx.msg_done 此时为 1 */
```

完整的 keep-alive epoll 服务端示例见 [`examples/server.c`](../examples/server.c)。

---

## chunked 解码器

独立的 chunked transfer-encoding 解码器，和流式状态机互不依赖。零拷贝：
解析出的块数据直接指向输入缓冲区。可一次喂一个字节而不破坏。

### `lht_chunked_init`

```c
void lht_chunked_init(lht_chunked_t *c);
```

清零状态。结构体本身可放栈上。

```c
typedef struct lht_chunked {
    uint64_t size;          /* 当前 chunk 大小 */
    uint64_t remaining;     /* 当前 chunk 数据剩余字节 */
    int      state;
    int      done;          /* 看到终止的 0 长度 chunk 后置 1 */
} lht_chunked_t;
```

### `lht_chunked_execute`

```c
size_t lht_chunked_execute(lht_chunked_t *c, const char *in, size_t in_len,
                           const char **out, size_t *out_len);
```

**参数**

- `in, in_len`：新输入字节。
- `out, out_len`：出参，当有完整 chunk 数据可用时，指向 `in` 内部（零拷贝）；
  否则 `*out_len = 0`。

**返回值**

本次消费的字节数。`(size_t)-1` 表示解码错误（chunk 格式非法）。调用方需把
剩余字节重新喂进来。`c->done` 为 1 时整个 chunked 流结束。

**示例**

```c
const char *in = "5\r\nHello\r\n6\r\n World\r\n0\r\n\r\n";
lht_chunked_t c;
lht_chunked_init(&c);

char out[256];
size_t off = 0;
const char *p = in;
size_t remain = strlen(in);

while (remain > 0) {
    const char *o;
    size_t olen;
    size_t consumed = lht_chunked_execute(&c, p, remain, &o, &olen);
    if (consumed == (size_t)-1) { /* 错误 */ break; }
    if (olen) {
        memcpy(out + off, o, olen);
        off += olen;
    }
    p += consumed;
    remain -= consumed;
    if (c.done) break;
}
/* out 现在是 "Hello World"，off == 11 */
```

逐字节喂的版本见 [`tests/test_chunked.c`](../tests/test_chunked.c)。

---

## URL 解析器

符合 RFC 3986。所有字段零拷贝指向 `url` 缓冲区。

### `lht_parse_url`

```c
int lht_parse_url(const char *url, size_t len, lht_url_t *out, int is_connect);
```

**参数**

- `url, len`：待解析的 URL。
- `out`：出参结构体，所有字段在成功时被填充。
- `is_connect`：非 0 表示按 CONNECT 请求的 authority 形式（`host:port`）解析。

**返回值**

`LHT_OK` 或 `LHT_EPARSE` / `LHT_EINVAL`。

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
```

**示例**

```c
lht_url_t u;
lht_parse_url("https://user:pw@example.com:8080/p?q=1#x",
              40, &u, 0);
/* u.scheme   = "https"
 * u.userinfo = "user:pw"
 * u.host     = "example.com"
 * u.port     = "8080"
 * u.path     = "/p"
 * u.query    = "q=1"
 * u.fragment = "x"
 */

lht_url_t v;
lht_parse_url("http://[::1]:8080/p", 19, &v, 0);
/* v.host = "::1"，v.port = "8080" */

lht_url_t w;
lht_parse_url("example.com:443", 15, &w, 1);  /* CONNECT */
/* w.host = "example.com"，w.port = "443" */
```

### `lht_percent_decode`

```c
size_t lht_percent_decode(const char *in, size_t len, char *out);
```

把 `in` percent-decode 到 `out`。`out` 至少要 `len` 字节。`%XX` 中的 `+`
也按空格处理（兼容 application/x-www-form-urlencoded）。畸形 `%XX` 序列原样
复制。返回解码后长度。

```c
char out[64];
size_t n = lht_percent_decode("hello%20world%21", 16, out);
out[n] = 0;  /* "hello world!" */

n = lht_percent_decode("a+b+c", 5, out);
out[n] = 0;  /* "a b c" */

n = lht_percent_decode("%ZZ", 3, out);
out[n] = 0;  /* "%ZZ" - 畸形原样保留 */
```

### `lht_parse_query`

```c
typedef struct lht_kv {
    const char *key;   size_t key_len;
    const char *value; size_t value_len;
} lht_kv_t;

size_t lht_parse_query(const char *query, size_t len,
                       lht_kv_t *pairs, size_t max_pairs);
```

把 query string 拆成键值对（按 `&` 分隔，`=` 切键值）。返回找到的对数（最多
`max_pairs`）。无 `=` 时 `value` / `value_len` 都是 0/NULL。所有指针指向
`query`。

```c
lht_kv_t kvs[8];
size_t n = lht_parse_query("a=1&b=2&flag", 11, kvs, 8);
/* kvs[0] = {"a", "1"}
 * kvs[1] = {"b", "2"}
 * kvs[2] = {"flag", ""}   (value_len == 0)
 */
```

---

## 头部辅助函数

零拷贝地操作 `(ptr, len)` 形式的头部。

### `lht_find_header`

```c
int lht_find_header(const lht_header_t *headers, size_t count,
                    const char *name, size_t name_len);
```

不区分大小写查头部名字。返回索引或 `-1`。

```c
int idx = lht_find_header(hdrs, nh, "Content-Type", 12);
if (idx >= 0)
    printf("%.*s\n", (int)hdrs[idx].value_len, hdrs[idx].value);
```

### `lht_header_has_token`

```c
int lht_header_has_token(const char *value, size_t len,
                         const char *token, size_t token_len);
```

在逗号分隔的列表中查指定 token，不区分大小写。常见用法是查
`Transfer-Encoding: chunked`、`Connection: keep-alive` 这类。

```c
if (lht_header_has_token(val, vlen, "chunked", 7)) {
    /* 走 chunked 路径 */
}
```

### `lht_strcasecmp`

```c
int lht_strcasecmp(const char *a, size_t alen, const char *b, size_t blen);
```

长度限定的 ASCII 不区分大小写比较。相等返回 0。

---

## SIMD / 运行时

### `lht_has_sse42` / `lht_has_avx2` / `lht_simd_backend`

```c
int lht_has_sse42(void);
int lht_has_avx2(void);
const char *lht_simd_backend(void); /* "avx2" | "sse42" | "scalar" */
```

CPU 特性检测。结果在第一次调用后缓存。`lht_simd_backend` 返回当前生效的
后端名字，方便 benchmark 自报。

```c
printf("backend: %s (sse4.2=%d, avx2=%d)\n",
       lht_simd_backend(), lht_has_sse42(), lht_has_avx2());
```

注意：同一份二进制只要编译时带了 `-mavx2` 或 `-msse4.2` 路径，运行时就会
自动按 CPU 能力派发到最快路径——不需要为不同 CPU 出不同包。

---

## 错误处理速查

| 返回值         | 含义                             | 一次性 | 流式 | chunked | URL  |
| -------------- | -------------------------------- | :----: | :--: | :-----: | :--: |
| `>=0` (int)    | body 偏移 / 成功                 | ✓      | —    | —       | —    |
| `LHT_OK` (0)   | 成功                             | —      | ✓    | —       | ✓    |
| `LHT_EAGAIN`   | 还要更多数据                     | ✓      | ✓    | —       | —    |
| `LHT_EPARSE`   | 格式错误                         | ✓      | ✓    | ✓       | ✓    |
| `LHT_EOVERFLOW` | 头太多 / 头太大                  | ✓      | ✓    | —       | —    |
| `LHT_EINVAL`   | 参数非法（如 NULL）              | ✓      | —    | —       | ✓    |
| `(size_t)-1`   | chunked 解码错误                 | —      | —    | ✓       | —    |

完整可运行示例见 [`examples/`](../examples/)：`simple.c`（一次性）、
`streaming.c`（流式）、`server.c`（epoll 服务端）、`cpp_example.cpp`
（C++ RAII 封装）。
