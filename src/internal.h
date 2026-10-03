/*
 * lightning internal header.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTNING_INTERNAL_H
#define LIGHTNING_INTERNAL_H

#include "lightning/lightning.h"

#include <string.h>

/* Detect x86 for runtime CPUID-based feature detection. */
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
#  define LHT_X86 1
#endif

/* Fast case-insensitive compare for ASCII tokens (methods, header names). */
static inline int lht_ascii_ieq(const char *a, size_t alen,
                                const char *b, size_t blen)
{
    if (alen != blen)
        return 0;
    while (alen--) {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb)
            return 0;
    }
    return 1;
}

/* Hex digit value, or -1. */
static inline int lht_hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Streaming parser state machine states. */
enum lht_stream_state {
    s_start = 0,
    s_req_method,
    s_req_method_sp,
    s_req_path,
    s_req_path_sp,
    s_req_http_h,
    s_req_http_ht,
    s_req_http_htt,
    s_req_http_http,
    s_req_http_slash,
    s_req_http_major,
    s_req_http_dot,
    s_req_http_minor,
    s_req_line_cr,
    s_req_line_lf,
    s_res_http_h,
    s_res_http_ht,
    s_res_http_htt,
    s_res_http_http,
    s_res_http_slash,
    s_res_http_major,
    s_res_http_dot,
    s_res_http_minor,
    s_res_status_sp,
    s_res_status,
    s_res_status_sp2,
    s_res_reason,
    s_res_line_cr,
    s_res_line_lf,
    s_header_field_start,
    s_header_field,
    s_header_value_discard_ws,
    s_header_value_start,
    s_header_value,
    s_header_value_lws,
    s_header_line_cr,
    s_header_line_lf,
    s_headers_done,
    s_chunk_size_start,
    s_chunk_size,
    s_chunk_parameters,
    s_chunk_size_cr,
    s_chunk_size_lf,
    s_chunk_data,
    s_chunk_data_cr,
    s_chunk_data_lf,
    s_chunk_trailer_start,
    s_body,
    s_message_done
};

/* Known HTTP methods (index stored in lht_stream.method). */
enum lht_method {
    LHT_M_DELETE = 1,
    LHT_M_GET,
    LHT_M_HEAD,
    LHT_M_POST,
    LHT_M_PUT,
    LHT_M_CONNECT,
    LHT_M_OPTIONS,
    LHT_M_TRACE,
    LHT_M_PATCH,
    LHT_M__OTHER = 0
};

#endif /* LIGHTNING_INTERNAL_H */
