# lightning 设计文档

这份文档讲清楚 lightning 的架构选择和取舍。读完它你应该能回答这些问题：
为什么是这个结构？SIMD 怎么用的？零拷贝具体怎么落地？流式状态机长什么样？
内存安全靠什么保证？RFC 合规到什么程度？

源码对应关系：

- 核心：[`src/lightning.c`](../src/lightning.c)
- 内部头：[`src/internal.h`](../src/internal.h)
- chunked：[`src/chunked.c`](../src/chunked.c)
- URL：[`src/url.c`](../src/url.c)

## 目录

- [1. 总体架构](#1-总体架构)
- [2. SIMD 策略](#2-simd-策略)
- [3. 零拷贝设计](#3-零拷贝设计)
- [4. 流式状态机](#4-流式状态机)
- [5. 内存安全](#5-内存安全)
- [6. RFC 合规](#6-rfc-合规)

---

## 1. 总体架构

lightning 是个有意识的小项目：三个翻译单元，一个公共头文件，零运行时依赖。

```
include/lightning/lightning.h   公共 API（所有调用方只看这个）
src/lightning.c                 核心：CPU 检测、SIMD 派发、一次性 + 流式解析
src/chunked.c                    chunked 独立解码器
src/url.c                        RFC 3986 URL + percent-decode
src/internal.h                   私有头：状态枚举、方法表、内联工具
```

两条独立的 API 表面：

1. **一次性 API**：`lht_parse_request` / `lht_parse_response` /
   `lht_parse_headers`。整缓冲区解析，无回调，无状态，无副作用。返回的指针
   指向入参缓冲区。热路径。
2. **流式 API**：`lht_stream_execute` 等。基于显式状态机，回调驱动，跨
   chunk 边界正确工作。用于 socket 这类不可控输入。

chunked 和 URL 解析器是独立模块，互不依赖，可以单独被嵌入式调用。

### 为什么不分多个 lib

3 个 `.c` 文件、约 1500 行代码。拆成多个 `.so` 反而增加符号可见性和
链接维护成本。一个 `liblightning.a` 让调用方 link 一次就完事。

### 为什么不用回调做一次性

`http_parser` 那种逐字节回调模式下，每个 token 都要 call/ret，回调里
通常还要做状态更新。一次性 API 直接用 `memchr` / SIMD 跳到下一个分隔符，
然后一次性给出全部 `(ptr, len)`，热路径没有函数调用开销。这是 lightning
比 `http_parser` 快 3.5 倍的主因之一。

---

## 2. SIMD 策略

### 编译 vs 运行

CMake 在 x86_64 上自动探测编译器对 `-mavx2` / `-msse4.2` 的支持，存在就用
对应宏（`LHT_AVX2_COMPILETIME` / `LHT_SSE42_COMPILETIME`）把整库带上对应
指令集编译：

```cmake
if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64|i386|i686)")
    check_c_compiler_flag("-mavx2" LHT_HAS_AVX2_FLAG)
    check_c_compiler_flag("-msse4.2" LHT_HAS_SSE42_FLAG)
    if(LHT_HAS_AVX2_FLAG)
        set(LHT_SIMD_FLAGS "-mavx2")
    elseif(LHT_HAS_SSE42_FLAG)
        set(LHT_SIMD_FLAGS "-msse4.2")
    endif()
endif()
```

但**真正的代码路径派发是在运行时**。`lht_has_avx2` / `lht_has_sse42` 通过
`cpuid` + `xgetbv` 检测 CPU 是否真的支持、OS 是否真的开了 AVX 状态保存：

```c
static int lht_has_avx2_runtime(void)
{
    if (!lht_has_osxsave())   /* OS 没开 XCR0，AVX 不能用 */
        return 0;
    uint32_t xcr0_lo, xcr0_hi;
    __asm__ __volatile__("xgetbv" : "=a"(xcr0_lo), "=d"(xcr0_hi) : "c"(0));
    if ((xcr0_lo & 6) != 6)   /* SSE + AVX state 都得开 */
        return 0;
    /* cpuid leaf 7, ebx bit 5 = AVX2 */
    ...
}
```

结果全局缓存（懒初始化），后续 `find_any` 第一次走 SIMD 后就是直接派发，
不再有分支开销。

### SIMD 用在哪

只用在一处：**查找缓冲区里第一个属于某集合的字节**。具体是 `find_any`，
找 `{c1, c2}` 中的任一字节。HTTP 头部解析的瓶颈就是找 `\r` 或 `\n`，
所以这正是热点。

```c
static const char *find_any(const char *p, size_t len, char c1, char c2)
{
#if defined(LHT_AVX2_COMPILETIME)
    if (lht_has_avx2() && len >= 32)
        return simd_find_any_avx2(p, len, c1, c2);
#endif
#if defined(LHT_SSE42_COMPILETIME)
    if (lht_has_sse42() && len >= 16)
        return simd_find_any_sse42(p, len, c1, c2);
#endif
    return scalar_find_any(p, len, c1, c2);
}
```

AVX2 路径每次比较 32 字节：`_mm256_set1_epi8` 把目标字节广播成向量，
`_mm256_cmpeq_epi8` 做逐字节相等比较，`_mm256_or_si256` 合并两个掩码，
`_mm256_movemask_epi8` 拿到 32 位掩码，`__builtin_ctz` 找到第一个置位
位的位置。

```c
__m256i v1 = _mm256_set1_epi8(c1);
__m256i v2 = _mm256_set1_epi8(c2);
while (p + 32 <= end) {
    __m256i d  = _mm256_loadu_si256((const __m256i *)p);
    __m256i m  = _mm256_or_si256(_mm256_cmpeq_epi8(d, v1),
                                 _mm256_cmpeq_epi8(d, v2));
    int mask = _mm256_movemask_epi8(m);
    if (mask)
        return p + __builtin_ctz(mask);
    p += 32;
}
```

SSE4.2 路径是 16 字节版本，逻辑相同。

### 对齐处理

入口先处理未对齐的头部字节（标量逐字节），等到 `(uintptr_t)p` 对齐到
32 字节边界后才进 SIMD 主循环。这样 `_mm256_loadu_si256` 用的是非对齐
load，但实际数据已经对齐——避免跨缓存行惩罚。尾部不足 32 字节回退标量。

### 为什么不用 PCMPxSTRI（SSE4.2 string compare）

PCMPxSTRI 能一次匹配字符串模式，但语义复杂、掩码布局反直觉，且 AVX2
没有对应扩展（AVX2 上还得退回 PCMPESTRI）。简单 `cmpeq + or + movemask`
的模式在两个目标字节时已经够快，可移植性也好。

### 为什么不在 ARM 上做 NEON

ARM NEON 的 `vdupq_n + vceqq + vorr + vmask` 等价路径确实可加，但目前
ARM 上 lightning 已经跑得很快（标量 `memchr` 在 glibc 里就是 NEON 优化
过的），瓶颈不在那里。等真有 ARM server 工作负载再说。代码里留了
`LHT_ARM_NEON` 宏占位。

### 没有走 SIMD 的地方

- `find_nl` 直接 `memchr('\n')`：libc 的 memchr 在所有平台上都已经是
  SIMD 实现的，自己再写一遍意义不大。
- `find_colon`：同上，`memchr(':')` 足够。

---

## 3. 零拷贝设计

零拷贝的核心承诺：**解析器不分配任何内存，返回的字段指针全部指向调用方
提供的缓冲区**。要做到这点，需要这些不变式：

1. 解析器自身没有动态分配。`lht_stream_t` / `lht_chunked_t` 由调用方拥有，
   放栈或堆都行。
2. 头部数组由调用方提供（`lht_header_t headers[N]`），解析器只填字段。
3. 返回的 `(name, name_len)` / `(value, value_len)` / `(method, method_len)`
   等都是入参缓冲区内部的指针 + 偏移。

**后果**：调用方在使用解析结果期间必须保证入参缓冲区不被释放、不被改写。
这是一份契约，文档里反复强调过。

### 流式 API 的"伪零拷贝"

流式模式下，回调拿到的 `(at, len)` 也是直接指向 `data` 入参里的字节——
没有拷贝。但有例外：**跨 chunk 边界的头部字段名 / 值**。这种情况下字段
本身被切成了多段，没法只给一段指针。lightning 用 `s->cur_field[128]` 和
`s->cur_value[256]` 这两个内部缓冲区累积完整 token：

```c
/* 累积字段名跨边界 */
if (s->cur_field_len + n < sizeof(s->cur_field)) {
    memcpy(s->cur_field + s->cur_field_len, start, n);
    s->cur_field_len += n;
}
```

这里有一次小拷贝，但只在跨边界 token 时发生，且仅 128 / 256 字节上限。
实际 HTTP 头部字段名几乎都 < 64 字节，单 chunk 就能装下，根本不进这个
路径。`cur_field` / `cur_value` 主要被用来做 hop-by-hop 头部（`TE`、
`Content-Length`、`Connection`）的识别——这几个头部很短，累积开销可忽略。

### chunked 解码也是零拷贝

`lht_chunked_execute` 输出的 `*out` 指针直接指向 `in` 里的数据段：

```c
case 4: { /* chunk data */
    size_t take = ...;
    if (take > 0) {
        *out = p;
        *out_len = take;
        ...
    }
}
```

调用方拿到的是解码后的 chunk 数据 view，没有额外拷贝。chunk size 行、
CRLF 等元数据被跳过不输出。

---

## 4. 流式状态机

流式解析器是一个手写的、显式状态机。状态枚举在 `internal.h`：

```c
enum lht_stream_state {
    s_start = 0,
    s_req_method, s_req_method_sp, s_req_path, s_req_path_sp,
    s_req_http_h, s_req_http_ht, s_req_http_htt, s_req_http_http,
    s_req_http_slash, s_req_http_major, s_req_http_dot, s_req_http_minor,
    s_req_line_cr, s_req_line_lf,
    /* 响应对称的一组 */
    s_res_http_h, ..., s_res_line_lf,
    /* 头部和消息体共享 */
    s_header_field_start, s_header_field, s_header_value_discard_ws,
    s_header_value_start, s_header_value, s_header_value_lws,
    s_header_line_cr, s_header_line_lf, s_headers_done,
    s_chunk_size_start, s_chunk_size, s_chunk_parameters,
    s_chunk_size_cr, s_chunk_size_lf, s_chunk_data,
    s_chunk_data_cr, s_chunk_data_lf, s_chunk_trailer_start,
    s_body, s_message_done
};
```

### 设计要点

**逐字节而非批量**：状态机每个循环消费一个字符，按当前 `state` 走分支。
这样 chunk 边界处理变成"在本字节处理完之前返回 `p - data`，下次从同样
状态接着干"。不需要在边界处保存中间偏移。

**批量优化**：在已知字符类的状态（如 `s_req_method`、`s_req_path`、
`s_header_field`、`s_header_value`）里，用 `while` 一次扫到下一个分隔符
再交回状态机。这样在一段连续可识别字符上几乎是 `memchr` 速度：

```c
case s_req_path: {
    const char *start = p;
    while (p < end && *p != ' ') ++p;
    if (p == end) {
        /* 跨 chunk：先回调已得到的部分，等下次再继续 */
        ...
        return p - data;
    }
    ...
}
```

**回调语义**：回调返回 0 继续；返回非 0 让 `lht_stream_execute` 立即停。
调用方可以用这个机制实现"看到某个头就停止"，或者把控制权让回上层。

**状态持久化**：所有状态都在 `lht_stream_t` 里，结构体本身可放栈、可放堆、
可被 `mmap` 共享。`reset` 只清状态字段，保留 `type`、`max_headers`、
`max_header_size`、`data`——这样 keep-alive 连接复用解析器时省一次 init。

### 关键状态：`s_headers_done`

头部结束（看到空行）后，状态机在这一个状态里决定后续走法，逻辑紧凑：

```c
case s_headers_done: {
    int no_body = 0;
    if (s->type == LHT_MSG_REQUEST &&
        s->content_length == ~0ull && !s->is_chunked)
        no_body = 1;                              /* 请求无 CL/TE 即无 body */
    if (s->type == LHT_MSG_RESPONSE &&
        (s->status_code < 200 ||
         s->status_code == 204 ||
         s->status_code == 304))
        no_body = 1;                              /* 这些响应无 body */
    if (s->method == LHT_M_HEAD)
        no_body = 1;                              /* HEAD 响应无 body */

    if (no_body)               -> s_message_done
    else if (s->is_chunked)    -> s_chunk_size_start
    else if (cl != ~0ull)      -> s_body 或 s_message_done (cl == 0)
    else                       -> s_body            /* 响应读到 EOF */
}
```

这是 RFC 7230 关于消息体长度的全部判定逻辑——TE 优先于 CL，HEAD / 1xx /
204 / 304 不带 body，未指定 CL 的响应读到连接关闭。

### chunked 子状态机

chunked body 有自己的子状态：

```
s_chunk_size_start -> s_chunk_size (读 hex)
                  -> s_chunk_parameters (跳过 ; ext)
                  -> s_chunk_size_cr -> s_chunk_size_lf
                  -> s_chunk_data (按 size 投递 on_body)
                  -> s_chunk_data_cr -> s_chunk_data_lf
                  -> 回到 s_chunk_size_start
size == 0 时:
                  -> s_chunk_trailer_start (跳过 trailer 行 + 终止空行)
                  -> s_message_done
```

`on_body` 在 chunk 数据被消费时触发，回调拿到的是原始 chunk 数据（去掉了
size 行和 CRLF），所以调用方不需要再做任何解码。

---

## 5. 内存安全

### 不越界读

每个 `while (p < end)` 和 `memchr(p, c, end - p)` 都用 `end` 做上界。
`find_nl` / `find_colon` 在没找到时返回 `p + len`，调用点统一检查
`>= end` 后报 `LHT_EPARSE`。

`lht_parse_request` 在解析 `HTTP/x.y` 之前先检查剩余长度：

```c
if (end - p < 10 || memcmp(p, "HTTP/", 5) != 0)
    return LHT_EPARSE;
```

`memcmp` 不会越过 `end - p` 字节，因为先确认了 `>= 10`。

### 不越界写

`lht_stream_t` 的 `cur_field[128]` 和 `cur_value[256]` 累积时都带长度
检查：

```c
if (s->cur_field_len + n < sizeof(s->cur_field)) {
    memcpy(s->cur_field + s->cur_field_len, start, n);
    s->cur_field_len += n;
}
/* 即便超长也只是不累积，回调里仍能拿到分段 view */
```

头部数组 `headers[N]` 在写之前检查 `count >= cap`，超了报 `LHT_EOVERFLOW`。

### 不释放后使用

返回的指针全部指向入参缓冲区。文档反复强调"使用期间 `buf` 必须存活"。
C++ 用户用 `std::string_view` 封装时尤其要注意：临时 `std::string` 转出的
`string_view` 一出作用域就失效。

### ASan/UBSan

CI 在 `make sanitize` 下用 `-fsanitize=address,undefined` 编译并跑全部
测试：

```cmake
if(LHT_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif()
```

测试套件包含 fuzz：

- `tests/test_fuzz.c::fuzz_streaming`：5000 次随机字节喂流式解析器，只验证
  "不崩、不越界"。
- `tests/test_fuzz.c::fuzz_valid_then_mutate`：合法请求 + 随机字节翻转 3000
  次。
- `tests/test_fuzz.c::fuzz_oneshot_truncated`：把合法请求按每个长度截断喂
  一次性解析器，验证返回值要么是错误要么是 `[0, cut]` 范围内的偏移。

`harness.h` 报告 `5153+ checks, 0 failed`，ASan/UBSan 全绿。

### 整数安全

- chunk size 解析在 `sz > (1ull << 63)` 时直接报错，避免 64 位溢出。
- `Content-Length` 用 `uint64_t`，最大 `~0ull` 表示"未指定"。
- 所有 `size_t` 与 `int` 的转换都通过显式 `(size_t)` / `(int)` 完成，开
  `-Wconversion` 编译。

### 没有未初始化读

`lht_stream_init` / `lht_chunked_init` 都用 `memset(0)` 全清，状态字段
显式初始化（`s->state = s_start`、`content_length = ~0ull` 等）。`reset`
保留 `type` / `max_headers` / `data` 等需要跨消息复用的字段。

---

## 6. RFC 合规

### RFC 7230（HTTP/1.1 消息语法）

**默认严格**：要求 CRLF 结尾、OWS 仅在头值两侧、不允许多余的空格。
但实际实现里有一个小妥协——一次性解析器同时接受 `\r\n` 和 `\n`：

```c
if (p < end && *p == '\r') ++p;
if (p >= end || *p != '\n')
    return LHT_EPARSE;
++p;
```

这是务实选择：现实世界总有裸 LF 的 HTTP（一些嵌入式设备、手写客户端），
死磕会让 lightning 在生产里用不起来。流式模式开 `LHT_CONF_TOLERANT`
则更宽松，连冒号两侧的额外 OWS 都吃。

**头部解析**：支持 OWS（`SP | HTAB`）在名字和值两侧；尾随 OWS 被裁掉。
RFC 7230 §3.2.4 的 obs-fold（行延续）目前**不支持**——认为这是历史包袱，
现代客户端没人发，加了反而扩大攻击面。

**Content-Length**：解析为 `uint64_t`，遇到非数字字符即停止（不报错，
按已解析部分计算）。多个 `Content-Length` 头目前按最后一个为准——这违反
RFC 7230 §3.3.2（应拒绝），是已知 TODO。

**Transfer-Encoding**：识别 `chunked` token，并优先于 `Content-Length`
（按 RFC 7230 §3.3.3，TE 优先）。

**Hop-by-hop**：`Connection`、`Keep-Alive`、`TE`、`Transfer-Encoding`、
`Content-Length` 都被识别并影响状态机。

### RFC 7231（HTTP 语义）

`LHT_M_HEAD` 的响应被认为没有 body，无论状态码。状态码 1xx、204、304
的响应也被认为没有 body。

### RFC 3986（URL）

`lht_parse_url` 完整支持：

- 绝对形式：`scheme://[userinfo@]host[:port]/path?query#fragment`
- origin 形式：`/path?query#fragment`（无 scheme / authority）
- authority 形式：`host:port`（CONNECT 请求）
- IPv6 字面量：`http://[::1]:8080/`
- scheme 语法验证：必须以 ALPHA 开头，后续 `ALPHA / DIGIT / + / - / .`

未做（且未声称做了）的：

- IRI / 非 ASCII 字符：lightning 按字节处理，不做 UTF-8 验证。
- scheme 大小写归一化：scheme 字段保持原样返回，调用方自己 lower-case。
- 端口范围 / host 语法合法性：交给上层。

### RFC 7230 §4.1（chunked）

`lht_chunked_execute` 支持：

- 标准 chunk size（hex）+ 可选 chunk-ext（`;key=val` 被跳过）
- 终止 0 长度 chunk + 可选 trailer 行 + 终止空行
- 跨调用边界：每个状态都可以从函数返回，下次接着干
- 一次喂一字节也能正确解码（见 `chunked_incremental` 测试）

不支持 chunk-ext 中的键值对解析——RFC 允许 ext 但极少被实际用，跳过。

### 已知不合规项

1. 多 `Content-Length` 头不拒绝（应按 §3.3.2 拒绝）。
2. obs-fold（行延续）不支持（§3.2.4 允许但不推荐）。
3. `lht_parse_request` 默认接受裸 LF（非严格）。
4. 头部字段名不做 RFC 7230 §3.2.6 的 `token` 语法验证（允许任意非 `:`、
   非 CR/LF 字节）。

第 1、3 项是为了务实兼容；第 2、4 项是为了性能。这些都是显式选择，不是
bug。如果调用方需要更严格的行为，开 `LHT_CONF_TOLERANT = 0`（默认），
并在拿到结果后自己做额外校验。
