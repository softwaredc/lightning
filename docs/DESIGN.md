# lightning design

This document covers the why behind the what. For exact signatures, see
[`API.md`](API.md); for measured throughput, see [`BENCHMARK.md`](BENCHMARK.md).

## Goals

1. **Throughput first.** A parser is on the hot path of every proxy, cache,
   and log processor. Parsing a 1 KB request head at 10 Gbps is ~100 ns per
   request; at that scale, allocator overhead and branch mispredictions
   dominate. So: no `malloc`, no `memcpy` of header bytes, no per-byte
   callbacks on the one-shot path.
2. **Memory-safe by construction.** The parser is fed untrusted bytes from a
   socket. It must never read past the end of the caller's buffer, never
   dereference a NULL, and never produce a `(ptr, len)` pair that escapes
   the buffer. ASan/UBSan clean is a release gate, not an aspiration.
3. **Streaming-friendly.** A socket `read()` can return any number of bytes
   at any boundary. The streaming parser must make progress on whatever it
   gets, without forcing the caller to buffer whole messages.
4. **Portable.** One source tree, one binary, runs on anything with a C99
   compiler. SIMD is a runtime fast-path, not a build-time requirement.

Non-goals: HTTP/2 (separate HPACK problem), TLS, connection management,
routing. lightning parses bytes; the caller decides what to do with them.

## Architecture

```
                 ┌────────────────────────────────────────────┐
   caller ─────▶ │  include/lightning/lightning.h             │  ◀── public API
                 │  one-shot  │  streaming  │  url  │ chunked │
                 └────────────┼───────────────────────────────┘
                              │
                 ┌───────────▼────────────────────────────────┐
                 │  src/lightning.c                            │
                 │  ┌─────────────────┐  ┌──────────────────┐  │
                 │  │  one-shot parser │  │ streaming FSM   │  │
                 │  └────────┬─────────┘  └──────────┬──────┘  │
                 │           │     SIMD byte search  │         │
                 │           └──────────┬─────────────┘         │
                 │           runtime dispatch: avx2 / sse42 /   │
                 │           scalar                                    │
                 └──────────────────────────────────────────────┘
                  src/chunked.c     src/url.c
                  (standalone)      (RFC 3986)
```

Three translation units, ~2k LoC total:

- **`lightning.c`** — one-shot parser, streaming state machine, header
  helpers, SIMD dispatch, CPU detection. The big one.
- **`chunked.c`** — incremental chunked decoder, usable without the rest.
- **`url.c`** — RFC 3986 URL splitter, percent-decoder, query parser.

`internal.h` is private. It declares the streaming state enum, the method
table, and the inline helpers (`lht_find_byte`, `lht_ascii_ieq`,
`lht_hexval`) that the .c files share. Public consumers must not include it.

## SIMD strategy

HTTP line scanning is, in the limit, "find the next `\n` in this buffer."
That is `memchr`, and `memchr` is one of the most optimized routines in
libc. So why bother with hand-written SIMD?

Because the one-shot parser spends most of its time not in `memchr`, but in
*header line* scanning — finding the `:` that separates field name from
value, then the `\n` that ends the line. Both scans happen over the same
run of bytes. Doing them as one pass over a 32-byte SIMD register avoids
two trips through L1.

The dispatch function:

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

Three things to notice:

1. **Runtime dispatch.** The binary is compiled with `-mavx2` (or
   `-msse4.2`) so the SIMD code is present, but it is gated by
   `lht_has_avx2()` / `lht_has_sse42()` which read CPUID on first call and
   cache the result. A binary built on a Zen 4 box runs scalar on a
   Haswell-era Xeon without crashing.
2. **Length gate.** For `len < 32` the SIMD path is a net loss (setup cost
   exceeds the saved work). The scalar fallback handles short buffers.
3. **Unaligned head.** The AVX2 path walks byte-by-byte until aligned,
   then switches to `__m256i` loads. Unaligned `_mm256_loadu_si256` is free
   on modern CPUs, but keeping the alignment dance makes the worst case
   predictable.

`memchr` is still used where the search is one-byte and the libc
implementation is excellent: request-line whitespace, response status
digits, the leading `\n` for `find_nl`. The hand-rolled SIMD only kicks in
for the two-byte (c1, c2) header scans, where it actually beats libc.

ARM NEON is currently scalar-only. The dispatch is in place; the
intrinsics are not. This is the highest-value contribution open.

## Zero-copy design

Every parsed field is returned as `(ptr, len)`, where `ptr` points into the
caller's input buffer. There is no internal staging buffer on the one-shot
path; headers are returned as `lht_header_t` entries with `name` and `value`
pointing directly into `buf`.

Consequences:

- The caller owns memory. lightning never `malloc`s.
- The returned pointers are valid only while `buf` is alive and unchanged.
  The caller is responsible for not freeing or mutating `buf` mid-parse.
- For header values, optional whitespace (OWS) is trimmed in-place by
  adjusting `value` / `value_len` — never by copying.

The streaming parser has one small staging area: `cur_field[128]` and
`cur_value[256]` on `lht_stream_t`. These accumulate a single field name or
value across byte-boundary splits (a `Content-Length` header split at
`Content-Len` would otherwise break the hop-by-hop detection logic). They
are fixed-size, owned by the caller (because the caller owns
`lht_stream_t`), and bounded by `max_header_size`. They are *not* used for
the user-facing callbacks — `on_header_field` and `on_header_value` still
receive pointers into the input buffer.

The `is_te_cl_conn` flag on the stream marks the current field as one of
`Transfer-Encoding`, `Content-Length`, `Connection` — the three headers
that drive body framing. When set, the parser copies the value into
`cur_value` to consult it after the line ends. This is the only place
lightning copies bytes during streaming parse, and the buffer is
stack-allocated inside the stream struct.

## Streaming state machine

The streaming parser is a flat switch over `s->state`. No nested function
calls, no recursion, no per-state allocation. Each state consumes some
bytes, transitions, and returns to the dispatcher.

States (see `internal.h` for the full enum):

```
s_start
 ├─ LHT_MSG_REQUEST ─▶ s_req_method ─▶ s_req_path ─▶ s_req_http_h ─▶ … ─▶ s_req_line_lf
 └─ LHT_MSG_RESPONSE ─▶ s_res_http_h ─▶ … ─▶ s_res_status ─▶ s_res_reason ─▶ s_res_line_lf
                                                          │
                                                          ▼
                              s_header_field_start ─▶ s_header_field ─▶ s_header_value_discard_ws
                                       ▲                                              │
                                       │                                              ▼
                                       │                                    s_header_value ─▶ s_header_line_cr
                                       │                                                          │
                                       ▼                                                          ▼
                                  s_header_line_lf ◀────────────────────────────────── s_header_line_lf
                                       │
                                       ▼
                              s_headers_done ─┬─▶ s_message_done          (no body)
                                              ├─▶ s_chunk_size_start     (chunked)
                                              ├─▶ s_body                 (Content-Length / read-until-EOF)
                                              └─▶ s_message_done         (HEAD method, 1xx/204/304)
```

Two invariants the FSM relies on:

1. **Every state that does multi-byte scanning can resume mid-token.** The
   `s_header_field` case shows this: it scans bytes until it sees `:`, `\r`,
   or `\n`. If it hits `end` first, it accumulates what it has into
   `cur_field`, sets `state = s_header_field` (not `s_header_field_start`),
   and returns. The next call re-enters the same case and continues the
   scan.

2. **Error transitions are terminal.** `stream_error(s)` sets
   `s->state = (uint32_t)-1` and the dispatcher hits the `default` case
   forever after. `lht_stream_error()` reports this. The only recovery is
   `lht_stream_reset()`.

The body decision happens in `s_headers_done`:

- Requests without `Content-Length` and without `Transfer-Encoding:
  chunked` have no body. → `s_message_done`.
- Responses with `1xx`, `204`, `304` have no body. → `s_message_done`.
- `HEAD` method requests have no body regardless. → `s_message_done`.
- Otherwise: chunked → chunk state machine; `Content-Length: N` →
  `s_body` for `N` bytes; nothing → `s_body` (read until EOF, valid for
  responses only).

`lht_stream_finish` is the EOF signal. For `s_body` with no
`Content-Length`, EOF is success. For any other non-`s_message_done`
state, EOF is `LHT_EPARSE` (truncation).

## Memory safety

The rules, enforced by the code and verified by ASan/UBSan in CI:

1. **No OOB reads.** Every scan checks `p < end` before dereferencing
   `*p`. SIMD loads check `p + 32 <= end` (AVX2) or `p + 16 <= end`
   (SSE4.2) before the load. The unaligned-head loop walks byte-by-byte
   until aligned, never touching bytes past `end`.

2. **No OOB writes.** `cur_field` and `cur_value` accumulation is bounded
   by `sizeof(s->cur_field) - s->cur_field_len > n`. If a header field is
   longer than 128 bytes, the field is silently truncated for the
   hop-by-hop detection (the user callback still gets the full pointer).

3. **No use-after-free.** Pointers returned to the caller point into the
   caller's buffer. The contract is documented: keep `buf` alive while
   you use the returned `(ptr, len)` pairs. lightning itself never holds
   a pointer across calls.

4. **No integer overflow.** `lht_chunked_execute` checks
   `sz > (1ull << 63)` before shifting. `Content-Length` parsing reads
   digits into a `uint64_t` with the same overflow check. Header counts
   are `size_t` and bounded by `max_headers` (default 1024).

5. **No UB.** Strict aliasing is respected: `__m256i` loads go through
   `__m256i*` casts of `const char*`, which is the documented pattern.
   Signed overflow is absent (we use `unsigned char` for byte
   classification). The fuzz test (`tests/test_fuzz.c`) feeds 5000 random
   inputs and 3000 mutated valid inputs through both parsers; ASan/UBSan
   stays clean.

## RFC compliance

Target: RFC 7230 (HTTP/1.1 message syntax) with selective tolerance for
real-world deviations.

**Strict (default, `LHT_CONF_NORMAL`):**

- Lines must end with `CRLF`. Bare `LF` is a parse error.
- No leading or trailing OWS allowed in field values (we trim it anyway,
  matching common practice).
- Request-target is opaque; we don't validate characters.
- Status code is exactly three digits.
- `Content-Length` is parsed as an unsigned integer; multiple
  `Content-Length` headers with different values would be a security bug
  (we don't currently detect this — see "Known limitations" below).

**Tolerant (`LHT_CONF_TOLERANT`):**

- Accepts bare `LF` as a line terminator.
- Accepts surrounding OWS in field names (rare in practice).

The streaming parser always accepts bare `LF` after a value, because too
many real servers send it. Strict CRLF enforcement happens in the
`s_header_line_cr` state when not in tolerant mode.

**Body framing rules (RFC 7230 §3.3):**

1. If `Transfer-Encoding: chunked` is present, the body is chunked.
   `Content-Length` is ignored (the spec says remove it; we just defer to
   chunked).
2. Otherwise, `Content-Length` gives the body length.
3. Otherwise, for responses, the body runs to EOF. For requests, no body.

This is the order the spec mandates. We do not currently reject messages
with both `Transfer-Encoding: chunked` and `Content-Length` — we treat
them as chunked. That matches every browser and server in the wild.

**Known limitations:**

- No multi-`Content-Length` conflict detection (security-relevant for
  proxies; tracked).
- No `Trailer` header parsing beyond consuming the trailer block.
- Headers with obs-fold (`\r\n` followed by whitespace as line
  continuation) are not merged; they are treated as separate fields,
  which the spec actually forbids but most parsers tolerate. Add later.
- ARM NEON SIMD path is unwritten; ARM falls through to scalar.
