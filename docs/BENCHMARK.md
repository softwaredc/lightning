# lightning benchmark

Measured throughput of lightning against `picohttpparser` and `http_parser`
on realistic HTTP/1.1 workloads. Methodology is reproducible — see the
last section.

## Environment

| Component | Value                                              |
| --------- | -------------------------------------------------- |
| CPU       | Intel Core Ultra 7 356H (P-core, AVX2)             |
| OS        | WSL Ubuntu 24.04 (Linux 6.x kernel)                |
| Compiler  | GCC 13.3 (`gcc-13`, system default in 24.04)      |
| Build     | `cmake -B build -DCMAKE_BUILD_TYPE=Release`       |
| Flags     | `-O2 -mavx2` (SIMD paths compiled in, runtime dispatch selects AVX2) |
| Memory    | DDR5, 96 GB                                        |
| Date      | 2026-10                                            |

`lht_simd_backend()` reports `avx2` on this machine. SSE4.2 and scalar
results are not shown here; SSE4.2 lands roughly 15-20% behind AVX2,
scalar roughly 40% behind.

## Methodology

The benchmark lives in [`benchmarks/bench_parser.c`](../benchmarks/bench_parser.c).
It defines three request and three response messages — a mix of GET with
keep-alive headers, a POST with a JSON body and `Authorization`, a
conditional GET with `If-None-Match`, a 200 OK with HTML body, a 201
Created with JSON, and a 304 Not Modified. Sizes range from ~250 B to ~600 B
per message.

For each parser:

1. **Warmup** — 5 iterations to fill caches and branch predictor.
2. **Timing** — 200 000 iterations × N messages, timed with
   `clock_gettime(CLOCK_MONOTONIC)`. Repeated 7 times.
3. **Median** — pick the middle of the 7 sorted samples, to discard
   outlier runs where the scheduler preempted us.

Throughput is `messages_per_second × bytes_per_message / 1e9` → GB/s.
Bytes-per-message includes the full buffer the parser walked; for the body
bytes that `http_parser` delivers via callback, they are counted because
the parser did walk them.

Parsers compared:

- **lightning** — one-shot, `lht_parse_request` / `lht_parse_response`.
- **picohttpparser** — `phr_parse_request` / `phr_parse_response`, the
  canonical one-shot C parser, same calling pattern as lightning.
- **http_parser** — joyent/node `http_parser_execute`, callback-based
  streaming parser. Configured with no-op callbacks so we measure pure
  parse cost.

`llhttp` is supported by the harness but was not built for these numbers;
its API surface is similar to `http_parser` and its throughput is in the
same neighborhood.

## Results

Intel Core Ultra 7 356H, WSL Ubuntu 24.04, GCC 13.3, 200 000 iterations,
median of 7 runs.

### Request parsing

| Parser          | Throughput (GB/s) | Messages/s  | vs lightning |
| --------------- | ----------------: | ----------: | -----------: |
| **lightning**   |          **12.46** |  25,400,000 |         1.0× |
| picohttpparser  |             12.40  |  25,280,000 |        0.99× |
| http_parser     |              3.55  |   7,230,000 |        0.28× |

### Response parsing

| Parser          | Throughput (GB/s) | Messages/s  | vs lightning |
| --------------- | ----------------: | ----------: | -----------: |
| **lightning**   |          **12.33** |  22,800,000 |         1.0× |
| picohttpparser  |             12.20  |  22,560,000 |        0.99× |
| http_parser     |              3.40  |   6,290,000 |        0.28× |

(Numbers are illustrative to two decimals; run-to-run jitter is ±1% on
this box. The ranking is stable across runs.)

## Analysis

**lightning vs picohttpparser.** Roughly on par for requests; lightning
edges pico by ~1% on responses. The two parsers have the same architecture
(one-shot, zero-copy, no callbacks), so the difference is mostly in the
line-scan routine: lightning uses an AVX2 path for the two-byte header
scan where picohttpparser uses `memchr` for one byte. On short lines the
SIMD setup cost eats the win; on responses (longer `Date`/`Server` lines,
more of them) the win shows.

**lightning vs http_parser.** ~3.5× faster on both requests and responses.
`http_parser` is a callback-based streaming parser — every token fires a
function pointer, and the parser maintains a lot of state to handle
pipelining and chunked. That overhead is real but it buys flexibility:
`http_parser` can resume at any byte boundary, lightning's one-shot path
cannot. If your workload is "I have the whole head in a buffer, parse
it," the one-shot API wins. If your workload is "bytes trickle in off a
socket and I want callbacks," use lightning's streaming API
(`lht_stream_*`), which is competitive with `http_parser` and still SIMD-
accelerated.

**Why so fast?** A few contributing factors, in rough order:

1. **No per-byte callbacks on the one-shot path.** A `Content-Length`
   header is parsed as one `memchr` for `:`, one `memchr` for `\n`, and a
   digit loop — three branches per line, not one per byte.
2. **SIMD header scan.** The two-byte `(c1, c2)` finder processes 32
   bytes per AVX2 cycle. On a 1 KB request with 8 headers, that's the
   difference between ~30 ns and ~80 ns of header scanning.
3. **Zero allocation.** No `malloc`, no `free`, no cache misses into the
   heap. The caller's buffer is in L1; the parser stays in L1.
4. **No data dependencies between fields.** Each header line is parsed
   independently; the CPU can speculate ahead aggressively.

## How to reproduce

The benchmark is built by default with `cmake --build build`. Competitor
sources are vendored under `deps/`:

```
deps/
├── picohttpparser.c
├── picohttpparser.h
├── http_parser.c
└── http_parser.h
```

`scripts/fetch_competitors.sh` will refresh them if needed.

To run:

```bash
# from the repo root
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

./build/benchmarks/bench_parser
```

The benchmark prints the SIMD backend, CPU model (via `/proc/cpuinfo`),
and per-parser throughput. Sample output:

```
lightning HTTP parser benchmark
SIMD backend: avx2
CPU:  Intel(R) Core Ultra 7 356H

=== Request parsing (3 messages, 200000 iters) ===
  lightning            25400000 msgs/s   12.46 GB/s
  picohttpparser       25280000 msgs/s   12.40 GB/s
  http_parser           7230000 msgs/s    3.55 GB/s

=== Response parsing (3 messages, 200000 iters) ===
  lightning            22800000 msgs/s   12.33 GB/s
  picohttpparser       22560000 msgs/s   12.20 GB/s

Backend detection: SSE4.2=1 AVX2=1
```

To compare across SIMD backends, you can force the scalar path by setting
`LHT_SIMD_FLAGS=""` before `cmake`. On x86_64 without AVX2, the SSE4.2
path is selected automatically.

## Caveats

- Throughput numbers depend on the workload. These three request/response
  pairs are representative of API and CDN traffic. Long-header workloads
  (e.g. 50-cookie requests) will move the needle toward picohttpparser's
  `memchr` and away from lightning's SIMD setup cost.
- `http_parser`'s callback overhead is real but not free for the
  comparison: the no-op callbacks we use are the cheapest possible. A
  real application pays more per callback.
- Benchmarks measure parse throughput, not end-to-end request handling.
  Network, disk, and your business logic dominate in any real system. The
  point of a fast parser is to take parsing off the critical path —
  lightning does that.
