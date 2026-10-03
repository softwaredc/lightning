# lightning - 极高性能 HTTP/1.1 解析器

一个为 C 语言写的、零拷贝、零分配的 HTTP/1.1 解析器。给代理、缓存、
日志处理器这类需要大口吞 HTTP 流量又不让 CPU 跪下的场景用的。

[![CI](https://github.com/your-org/lightning/actions/workflows/ci.yml/badge.svg)](https://github.com/your-org/lightning/actions)
[![License: Apache-2.0](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
[![C99](https://img.shields.io/badge/language-C99-lightgrey.svg)](https://en.cppreference.com/w/c/99)

`lightning` 在 x86_64 上以接近内存带宽的速度解析 HTTP/1.x 请求和响应头。热路径
不调用 `malloc`、不拷贝字节、不越过你交给它的缓冲区边界。它是一个单一的静态库
（约 3 个翻译单元），带有可选的 AVX2/SSE4.2 路径在运行时检测，所以同一份二进制
可以到处跑，自动用最快的路径。

## 功能特性

- **热路径零分配。** 调用方拥有全部内存。没有 `malloc`、没有 `free`、没有隐藏
  缓冲区。
- **零拷贝。** 每个解析出的字段都是一对 `(ptr, len)`，直接指向调用方缓冲区里的
  数据。没有 `strdup`、没有对头值的 `memcpy`。
- **SIMD 加速的头扫描。** Intel 和 AMD Zen+ 上用 AVX2（每周期 32 字节）；其他
  x86 上用 SSE4.2（每周期 16 字节）。ARM/PPC 有标量回退。
- **一次性 API 和流式 API 都有。** 一次性适合整缓冲区解析（代理、缓存、日志
  解析器）。流式适合从 socket 读到的部分数据，按任意分块边界推进。
- **chunked 传输编码解码器。** 增量、零拷贝，可以一次喂一个字节而不破坏。
- **RFC 3986 URL 解析器。** 拆分 scheme、userinfo、host（含 IPv6 字面量）、
  port、path、query、fragment。带 percent-decoder 和 query-string 拆分器。
- **多语言绑定。** Python (ctypes)、Go (cgo)、Rust (FFI)、Node.js (ffi-napi) ——
  见 [`bindings/`](bindings/)。
- **默认严格，请求可放宽。** 默认走 RFC 7230 严格模式；`LHT_CONF_TOLERANT`
  可以接受裸 LF 和围绕冒号的 OWS。
- **ASan/UBSan 全绿。** 所有解析器 5153+ 项检查全部通过；fuzz 输入下无泄漏、
  无越界读、无 UB。

## 快速开始

```bash
cmake -B build && cmake --build build
```

这条命令产出 `build/liblightning.a`（和 `.so`），外加测试、benchmark 和示例。
`make test` 跑测试套件，`make bench` 跑 benchmark。

一次性解析就五行：

```c
#include "lightning/lightning.h"

const char *req = "GET /hello HTTP/1.1\r\nHost: example.com\r\n\r\n";
lht_header_t hdrs[16];
size_t nh = 16;
const char *method, *path;
size_t mlen, plen;
int minor;
int body_off = lht_parse_request(req, strlen(req),
                                 &method, &mlen, &path, &plen,
                                 &minor, hdrs, &nh);
/* body_off >= 0  -> 请求体在 `req` 中的起始偏移 */
/* body_off <  0  -> LHT_EPARSE / LHT_EAGAIN / LHT_EOVERFLOW / LHT_EINVAL */
```

返回的所有指针（`method`、`path`、`hdrs[i].name`、`hdrs[i].value`）都直接指向
`req` 内部。在使用它们期间 `req` 必须保持存活。

完整 API 参考：[`docs/API_CN.md`](docs/API_CN.md)。

## 性能

Intel Core Ultra 7 356H，WSL Ubuntu 24.04，GCC 13.3，`-O2`，每个解析器跑 20 万
次迭代，7 次取中位数。方法和复现步骤见 [`docs/BENCHMARK_CN.md`](docs/BENCHMARK_CN.md)。

| 解析器           | 请求 GB/s | 响应 GB/s | 备注                          |
| --------------- | --------: | --------: | ----------------------------- |
| **lightning**   | **12.46** | **12.33** | AVX2 后端                     |
| picohttpparser  |    12.40  |    12.20  | 一次性，可比                  |
| http_parser     |     3.55  |     3.40  | joyent/node，基于回调         |

lightning 在请求解析上和 picohttpparser 持平，响应解析略快一点。对
`http_parser` 快约 3.5 倍，主因是一次性路径跳过了逐字节回调，并用 SIMD 做
行扫描。

## API 概览

| 表面                  | 入口                                                |
| --------------------- | --------------------------------------------------- |
| 一次性解析器          | `lht_parse_request`、`lht_parse_response`、`lht_parse_headers` |
| 流式解析器            | `lht_stream_init`、`lht_stream_execute`、`lht_stream_finish`、`lht_stream_reset`、`lht_stream_error` |
| chunked 解码器        | `lht_chunked_init`、`lht_chunked_execute`          |
| URL 解析器            | `lht_parse_url`、`lht_percent_decode`、`lht_parse_query` |
| 头部辅助函数          | `lht_find_header`、`lht_header_has_token`、`lht_strcasecmp`、`lht_method_str`、`lht_method_match` |
| SIMD / 运行时         | `lht_has_sse42`、`lht_has_avx2`、`lht_simd_backend` |

签名、语义和逐函数示例在 [`docs/API_CN.md`](docs/API_CN.md)。设计原理——
SIMD 策略、流式状态机、内存安全不变式、RFC 合规说明——在
[`docs/DESIGN_CN.md`](docs/DESIGN_CN.md)。

## 多语言绑定

| 语言    | 机制        | 路径                              | 状态            |
| ------- | ----------- | --------------------------------- | --------------- |
| Python  | ctypes      | `bindings/python/`                | 一次性解析      |
| Go      | cgo         | `bindings/go/lightning/`          | 一次性解析      |
| Rust    | FFI + safe  | `bindings/rust/lightning-sys/`    | 一次性解析      |
| Node.js | ffi-napi    | `bindings/nodejs/`                | 一次性解析      |

绑定都封装的是一次性 C API。流式和 chunked API 目前只有 C 接口；用法见
`bindings/<lang>/example.*`。CI 在每次提交时构建并运行所有绑定。

## 项目结构

```
lightning/
├── include/lightning/lightning.h   # 公共 API，单头文件
├── src/
│   ├── lightning.c                 # 核心：一次性 + 流式 + SIMD 派发
│   ├── chunked.c                   # 独立的 chunked 解码器
│   ├── url.c                       # RFC 3986 URL 解析器 + percent-decode
│   └── internal.h                  # 私有头，状态机，方法表
├── tests/                          # 无依赖测试框架，5153+ 项检查
├── benchmarks/                     # bench_parser.c + 竞品源码
├── examples/                       # simple.c、streaming.c、server.c (epoll)
├── bindings/                       # python、go、rust、nodejs
├── deps/                           # picohttpparser、http_parser（用于 benchmark）
├── scripts/                        # 竞品抓取脚本
├── docs/                           # API_CN.md、DESIGN_CN.md、BENCHMARK_CN.md
├── CMakeLists.txt
└── Makefile                        # make / make test / make bench / make sanitize
```

## 许可证

Apache-2.0。见 [`LICENSE`](LICENSE)。
