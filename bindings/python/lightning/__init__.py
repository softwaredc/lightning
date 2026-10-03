"""
lightning - Python ctypes binding for the lightning HTTP/1.1 parser.

A thin FFI wrapper over liblightning.  Zero-copy by design: parsed
fields reference the original Python bytes object, so the caller must
keep it alive while the fields are in use.

    pip install -e .        # or: python setup.py install

Usage:
    import lightning
    req = lightning.parse_request(b"GET /hello HTTP/1.1\\r\\nHost: x\\r\\n\\r\\n")
    print(req.method, req.path, req.headers)
"""
from __future__ import annotations
import ctypes
import ctypes.util
import os
import sys
from dataclasses import dataclass, field
from typing import List, Optional, Tuple

# ---------------------------------------------------------------------------
# Library loading
# ---------------------------------------------------------------------------

def _find_library() -> ctypes.CDLL:
    # 1. explicit override
    env = os.environ.get("LIGHTNING_LIB")
    if env and os.path.isfile(env):
        return ctypes.CDLL(env)
    # 2. sibling path relative to this file (in-tree build)
    here = os.path.dirname(os.path.abspath(__file__))
    for _ in range(4):
        cand = os.path.join(here, "build", "liblightning.so")
        if os.path.isfile(cand):
            return ctypes.CDLL(cand)
        # also look for a 'lib' dir
        cand = os.path.join(here, "lib", "liblightning.so")
        if os.path.isfile(cand):
            return ctypes.CDLL(cand)
        here = os.path.dirname(here)
    # 3. system search
    found = ctypes.util.find_library("lightning")
    if found:
        return ctypes.CDLL(found)
    raise OSError(
        "liblightning not found. Build it first:\n"
        "  cmake -B build && cmake --build build\n"
        "Or set LIGHTNING_LIB=/path/to/liblightning.so"
    )

_lib = _find_library()

# ---------------------------------------------------------------------------
# C type definitions
# ---------------------------------------------------------------------------

class _CHeader(ctypes.Structure):
    _fields_ = [
        ("name", ctypes.c_char_p),
        ("name_len", ctypes.c_size_t),
        ("value", ctypes.c_char_p),
        ("value_len", ctypes.c_size_t),
    ]

# Function prototypes
_lib.lht_parse_request.argtypes = [
    ctypes.c_char_p, ctypes.c_size_t,
    ctypes.POINTER(ctypes.c_char_p), ctypes.POINTER(ctypes.c_size_t),
    ctypes.POINTER(ctypes.c_char_p), ctypes.POINTER(ctypes.c_size_t),
    ctypes.POINTER(ctypes.c_int),
    ctypes.POINTER(_CHeader), ctypes.POINTER(ctypes.c_size_t),
]
_lib.lht_parse_request.restype = ctypes.c_int

_lib.lht_parse_response.argtypes = [
    ctypes.c_char_p, ctypes.c_size_t,
    ctypes.POINTER(ctypes.c_int),
    ctypes.POINTER(ctypes.c_char_p), ctypes.POINTER(ctypes.c_size_t),
    ctypes.POINTER(ctypes.c_int),
    ctypes.POINTER(_CHeader), ctypes.POINTER(ctypes.c_size_t),
]
_lib.lht_parse_response.restype = ctypes.c_int

_lib.lht_parse_url.argtypes = [
    ctypes.c_char_p, ctypes.c_size_t,
    ctypes.POINTER(ctypes.c_void_p),  # will use raw struct
    ctypes.c_int,
]
_lib.lht_parse_url.restype = ctypes.c_int

_lib.lht_simd_backend.argtypes = []
_lib.lht_simd_backend.restype = ctypes.c_char_p

_lib.lht_has_sse42.argtypes = []
_lib.lht_has_sse42.restype = ctypes.c_int

_lib.lht_has_avx2.argtypes = []
_lib.lht_has_avx2.restype = ctypes.c_int

# ---------------------------------------------------------------------------
# Public dataclasses
# ---------------------------------------------------------------------------

@dataclass
class Header:
    name: bytes
    value: bytes

    def __repr__(self) -> str:
        return f"Header({self.name.decode()}: {self.value.decode()})"

@dataclass
class HttpRequest:
    method: bytes
    path: bytes
    minor_version: int
    headers: List[Header]
    body_offset: int

    def header(self, name: str) -> Optional[bytes]:
        name_lower = name.lower().encode()
        for h in self.headers:
            if h.name.lower() == name_lower:
                return h.value
        return None

@dataclass
class HttpResponse:
    status_code: int
    reason: bytes
    minor_version: int
    headers: List[Header]
    body_offset: int

    def header(self, name: str) -> Optional[bytes]:
        name_lower = name.lower().encode()
        for h in self.headers:
            if h.name.lower() == name_lower:
                return h.value
        return None

# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

_MAX_HEADERS = 256

def parse_request(data: bytes) -> HttpRequest:
    """Parse a full HTTP/1.x request from a bytes buffer."""
    buf = ctypes.create_string_buffer(data)  # mutable copy so c_char_p is stable
    method = ctypes.c_char_p()
    method_len = ctypes.c_size_t()
    path = ctypes.c_char_p()
    path_len = ctypes.c_size_t()
    minor = ctypes.c_int()
    headers = (_CHeader * _MAX_HEADERS)()
    num = ctypes.c_size_t(_MAX_HEADERS)

    ret = _lib.lht_parse_request(
        buf, len(data),
        ctypes.byref(method), ctypes.byref(method_len),
        ctypes.byref(path), ctypes.byref(path_len),
        ctypes.byref(minor),
        headers, ctypes.byref(num),
    )
    if ret < 0:
        _raise_error(ret)

    hs = []
    for i in range(num.value):
        nm = ctypes.string_at(headers[i].name, headers[i].name_len)
        vl = ctypes.string_at(headers[i].value, headers[i].value_len)
        hs.append(Header(nm, vl))

    return HttpRequest(
        method=ctypes.string_at(method, method_len.value),
        path=ctypes.string_at(path, path_len.value),
        minor_version=minor.value,
        headers=hs,
        body_offset=ret,
    )

def parse_response(data: bytes) -> HttpResponse:
    """Parse a full HTTP/1.x response from a bytes buffer."""
    buf = ctypes.create_string_buffer(data)
    status = ctypes.c_int()
    reason = ctypes.c_char_p()
    reason_len = ctypes.c_size_t()
    minor = ctypes.c_int()
    headers = (_CHeader * _MAX_HEADERS)()
    num = ctypes.c_size_t(_MAX_HEADERS)

    ret = _lib.lht_parse_response(
        buf, len(data),
        ctypes.byref(status),
        ctypes.byref(reason), ctypes.byref(reason_len),
        ctypes.byref(minor),
        headers, ctypes.byref(num),
    )
    if ret < 0:
        _raise_error(ret)

    hs = []
    for i in range(num.value):
        nm = ctypes.string_at(headers[i].name, headers[i].name_len)
        vl = ctypes.string_at(headers[i].value, headers[i].value_len)
        hs.append(Header(nm, vl))

    return HttpResponse(
        status_code=status.value,
        reason=ctypes.string_at(reason, reason_len.value),
        minor_version=minor.value,
        headers=hs,
        body_offset=ret,
    )

def simd_backend() -> str:
    """Return the active SIMD backend: 'avx2', 'sse42', or 'scalar'."""
    return _lib.lht_simd_backend().decode()

def has_sse42() -> bool:
    return bool(_lib.lht_has_sse42())

def has_avx2() -> bool:
    return bool(_lib.lht_has_avx2())

_ERROR_MSGS = {
    -1: "need more data",
    -2: "parse error: malformed input",
    -3: "overflow: too many headers or header too large",
    -4: "invalid argument",
    -5: "feature not compiled in",
}

def _raise_error(code: int) -> None:
    raise ValueError(f"lightning error {code}: {_ERROR_MSGS.get(code, 'unknown')}")
