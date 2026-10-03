// Package lightning provides Go bindings for the lightning HTTP/1.1 parser.
//
// Zero-copy by design: ByteSlice values reference the original input buffer.
// Keep the input alive while the returned slices are in use.
//
// Build the C library first:
//
//	cmake -B build && cmake --build build
//
// Then in Go:
//
//	req, err := lightning.ParseRequest(data)
//	fmt.Println(req.Method, req.Path)
package lightning

/*
#cgo CFLAGS: -I${SRCDIR}/../../../include
#cgo LDFLAGS: -L${SRCDIR}/../../../build -llightning

#include <lightning/lightning.h>
#include <stdlib.h>

// Helper: parse a request and fill a results struct.
// Go can't easily deal with pointer-to-pointer, so we wrap it.
struct req_result {
    const char *method;    size_t method_len;
    const char *path;      size_t path_len;
    int minor_version;
    int body_offset;
    int error;
    // headers inline (max 256)
    const char *h_name[256];   size_t h_name_len[256];
    const char *h_value[256];  size_t h_value_len[256];
    size_t h_count;
};

static struct req_result parse_req(const char *buf, size_t len) {
    struct req_result r = {0};
    lht_header_t headers[256];
    size_t n = 256;
    int ret = lht_parse_request(buf, len,
        &r.method, &r.method_len,
        &r.path, &r.path_len,
        &r.minor_version, headers, &n);
    if (ret < 0) { r.error = ret; return r; }
    r.body_offset = ret;
    r.h_count = n;
    for (size_t i = 0; i < n; i++) {
        r.h_name[i] = headers[i].name;
        r.h_name_len[i] = headers[i].name_len;
        r.h_value[i] = headers[i].value;
        r.h_value_len[i] = headers[i].value_len;
    }
    return r;
}

struct res_result {
    int status_code;
    const char *reason;  size_t reason_len;
    int minor_version;
    int body_offset;
    int error;
    const char *h_name[256];   size_t h_name_len[256];
    const char *h_value[256];  size_t h_value_len[256];
    size_t h_count;
};

static struct res_result parse_res(const char *buf, size_t len) {
    struct res_result r = {0};
    lht_header_t headers[256];
    size_t n = 256;
    int ret = lht_parse_response(buf, len,
        &r.status_code,
        &r.reason, &r.reason_len,
        &r.minor_version, headers, &n);
    if (ret < 0) { r.error = ret; return r; }
    r.body_offset = ret;
    r.h_count = n;
    for (size_t i = 0; i < n; i++) {
        r.h_name[i] = headers[i].name;
        r.h_name_len[i] = headers[i].name_len;
        r.h_value[i] = headers[i].value;
        r.h_value_len[i] = headers[i].value_len;
    }
    return r;
}

static const char *simd_backend(void) { return lht_simd_backend(); }
*/
import "C"

import (
	"errors"
	"fmt"
	"unsafe"
)

// Header is a single HTTP header field.
type Header struct {
	Name  string
	Value string
}

// Request is a parsed HTTP request.
type Request struct {
	Method       string
	Path         string
	MinorVersion int
	Headers      []Header
	BodyOffset   int
}

// Response is a parsed HTTP response.
type Response struct {
	StatusCode   int
	Reason       string
	MinorVersion int
	Headers      []Header
	BodyOffset   int
}

func (r *Request) Header(name string) (string, bool) {
	for _, h := range r.Headers {
		if equalFold(h.Name, name) {
			return h.Value, true
		}
	}
	return "", false
}

func (r *Response) Header(name string) (string, bool) {
	for _, h := range r.Headers {
		if equalFold(h.Name, name) {
			return h.Value, true
		}
	}
	return "", false
}

var errs = map[int]string{
	-1: "need more data",
	-2: "malformed input",
	-3: "too many headers or header too large",
	-4: "invalid argument",
	-5: "feature not compiled in",
}

// ParseRequest parses a full HTTP/1.x request from buf.
func ParseRequest(buf []byte) (*Request, error) {
	if len(buf) == 0 {
		return nil, errors.New("empty input")
	}
	cBuf := (*C.char)(unsafe.Pointer(&buf[0]))
	cLen := C.size_t(len(buf))
	r := C.parse_req(cBuf, cLen)
	if r.error != 0 {
		return nil, fmt.Errorf("lightning: %s (code %d)", errs[int(r.error)], r.error)
	}

	req := &Request{
		Method:       C.GoStringN(r.method, C.int(r.method_len)),
		Path:         C.GoStringN(r.path, C.int(r.path_len)),
		MinorVersion: int(r.minor_version),
		BodyOffset:   int(r.body_offset),
	}
	for i := C.size_t(0); i < r.h_count; i++ {
		req.Headers = append(req.Headers, Header{
			Name:  C.GoStringN(r.h_name[i], C.int(r.h_name_len[i])),
			Value: C.GoStringN(r.h_value[i], C.int(r.h_value_len[i])),
		})
	}
	return req, nil
}

// ParseResponse parses a full HTTP/1.x response from buf.
func ParseResponse(buf []byte) (*Response, error) {
	if len(buf) == 0 {
		return nil, errors.New("empty input")
	}
	cBuf := (*C.char)(unsafe.Pointer(&buf[0]))
	cLen := C.size_t(len(buf))
	r := C.parse_res(cBuf, cLen)
	if r.error != 0 {
		return nil, fmt.Errorf("lightning: %s (code %d)", errs[int(r.error)], r.error)
	}

	resp := &Response{
		StatusCode:   int(r.status_code),
		Reason:       C.GoStringN(r.reason, C.int(r.reason_len)),
		MinorVersion: int(r.minor_version),
		BodyOffset:   int(r.body_offset),
	}
	for i := C.size_t(0); i < r.h_count; i++ {
		resp.Headers = append(resp.Headers, Header{
			Name:  C.GoStringN(r.h_name[i], C.int(r.h_name_len[i])),
			Value: C.GoStringN(r.h_value[i], C.int(r.h_value_len[i])),
		})
	}
	return resp, nil
}

// SimdBackend returns the active SIMD backend name.
func SimdBackend() string {
	return C.GoString(C.simd_backend())
}

func equalFold(a, b string) bool {
	if len(a) != len(b) {
		return false
	}
	for i := 0; i < len(a); i++ {
		ca, cb := a[i], b[i]
		if ca >= 'A' && ca <= 'Z' {
			ca += 32
		}
		if cb >= 'A' && cb <= 'Z' {
			cb += 32
		}
		if ca != cb {
			return false
		}
	}
	return true
}
