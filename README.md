# lightning - Extremely Fast HTTP/1.1 Parser

A small, zero-copy, zero-allocation HTTP/1.1 parser for C. Built for proxies,
caches, log processors, and anything that has to swallow a lot of HTTP traffic
without melting a CPU.

[![CI](https://github.com/your-org/lightning/actions/workflows/ci.yml/badge.svg)](https://github.com/your-org/lightning/actions)
[![License: Apache-2.0](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
[![C99](https://img.shields.io/badge/language-C99-lightgrey.svg)](https://en.cppreference.com/w/c/99)

`lightning` parses HTTP/1.x request and response heads at memory bandwidth on
x86_64. The hot path does not call `malloc`, does not copy bytes, and does not
read past the end of the buffer you hand it. It is a single static library
(~3 translation units) with optional AVX2/SSE4.2 paths that are runtime-
detected, so the same binary runs everywhere and uses the fastest path
available.

## Features

- **Zero allocation on the hot path.** Callers own all memory. No `malloc`,
  no `free`, no hidden buffers.
- **Zero copy.** Every parsed field is a `(ptr, len)` pair that points
  directly into the caller's buffer. No `strdup`, no `memcpy` of header
  values.
- **SIMD-accelerated header scanning.** AVX2 (32 bytes/cycle) on Intel and
  AMD Zen+; SSE4.2 (16 bytes/cycle) elsewhere. Scalar fallback for ARM/PPC.
- **Both one-shot and streaming APIs.** One-shot for full-buffer parsing
  (proxies, caches, log parsers). Streaming for partial reads from a socket
  with arbitrary chunk boundaries.
- **Chunked transfer-encoding decoder.** Incremental, zero-copy, can be
  driven one byte at a time without breaking.
- **RFC 3986 URL parser.** Splits scheme, userinfo, host (incl. IPv6
  literals), port, path, query, fragment. Percent-decoder and query-string
  splitter included.
- **Multi-language bindings.** Python (ctypes), Go (cgo), Rust (FFI),
  Node.js (ffi-napi) — see [`bindings/`](bindings/).
- **Strict by default, tolerant on request.** RFC 7230 strict mode is the
  default; `LHT_CONF_TOLERANT` accepts bare LF and surrounding OWS.
- **Clean under ASan/UBSan.** 5153+ checks across all parsers; no leaks, no
  OOB reads, no UB on fuzzed input.

## Quick start

```bash
cmake -B build && cmake --build build
```

That gives you `build/liblightning.a` (and `.so`), plus tests, benchmarks, and
examples. Use `make test` to run the test suite, or `make bench` to run the
benchmark.

A one-shot parse is five lines:

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
/* body_off >= 0  -> offset of body in `req` */
/* body_off <  0  -> LHT_EPARSE / LHT_EAGAIN / LHT_EOVERFLOW / LHT_EINVAL */
```

All returned pointers (`method`, `path`, `hdrs[i].name`, `hdrs[i].value`)
point into `req` itself. Keep `req` alive while you use them.

Full API reference: [`docs/API.md`](docs/API.md).

## Benchmark

Intel Core Ultra 7 356H, WSL Ubuntu 24.04, GCC 13.3, `-O2`, 200k iterations
per parser, median of 7 runs. See [`docs/BENCHMARK.md`](docs/BENCHMARK.md)
for methodology and repro steps.

| Parser          | Req GB/s | Resp GB/s | Notes                          |
| --------------- | -------: | --------: | ------------------------------ |
| **lightning**   | **12.46** | **12.33** | AVX2 backend                   |
| picohttpparser  |    12.40 |    12.20  | One-shot, comparable           |
| http_parser     |     3.55 |     3.40  | joyent/node, callback-based    |

lightning lands on par with picohttpparser on requests and edges it slightly
on responses. Against `http_parser` it is ~3.5x faster, primarily because the
one-shot path skips per-byte callbacks and uses SIMD for line scanning.

## API overview

| Surface                  | Entry points                                        |
| ------------------------ | --------------------------------------------------- |
| One-shot parsers         | `lht_parse_request`, `lht_parse_response`, `lht_parse_headers` |
| Streaming parser         | `lht_stream_init`, `lht_stream_execute`, `lht_stream_finish`, `lht_stream_reset`, `lht_stream_error` |
| Chunked decoder          | `lht_chunked_init`, `lht_chunked_execute`          |
| URL parser               | `lht_parse_url`, `lht_percent_decode`, `lht_parse_query` |
| Header helpers           | `lht_find_header`, `lht_header_has_token`, `lht_strcasecmp`, `lht_method_str`, `lht_method_match` |
| SIMD / runtime           | `lht_has_sse42`, `lht_has_avx2`, `lht_simd_backend` |

Signatures, semantics, and per-function examples live in
[`docs/API.md`](docs/API.md). The design rationale — SIMD strategy, the
streaming state machine, memory-safety invariants, RFC compliance notes —
is in [`docs/DESIGN.md`](docs/DESIGN.md).

## Multi-language bindings

| Language | Mechanism   | Path                              | Status          |
| -------- | ----------- | --------------------------------- | --------------- |
| Python   | ctypes      | `bindings/python/`                | One-shot parse  |
| Go       | cgo         | `bindings/go/lightning/`          | One-shot parse  |
| Rust     | FFI + safe  | `bindings/rust/lightning-sys/`    | One-shot parse  |
| Node.js  | ffi-napi    | `bindings/nodejs/`                | One-shot parse  |

Bindings wrap the one-shot C API. The streaming and chunked APIs are C-only
for now; see `bindings/<lang>/example.*` for usage. CI builds and runs every
binding on every push.

## Project structure

```
lightning/
├── include/lightning/lightning.h   # Public API, single header
├── src/
│   ├── lightning.c                 # Core: one-shot + streaming + SIMD dispatch
│   ├── chunked.c                   # Standalone chunked decoder
│   ├── url.c                       # RFC 3986 URL parser + percent-decode
│   └── internal.h                  # Private header, state machine, method table
├── tests/                          # No-deps test harness, 5153+ checks
├── benchmarks/                     # bench_parser.c + competitor sources
├── examples/                       # simple.c, streaming.c, server.c (epoll)
├── bindings/                       # python, go, rust, nodejs
├── deps/                           # picohttpparser, http_parser (for bench)
├── scripts/                        # competitor fetchers
├── docs/                           # API.md, DESIGN.md, BENCHMARK.md
├── CMakeLists.txt
└── Makefile                        # make / make test / make bench / make sanitize
```

## License

Apache-2.0. See [`LICENSE`](LICENSE).
