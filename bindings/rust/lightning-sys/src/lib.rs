//! Raw FFI bindings to liblightning.
//!
//! See the `safe` module for a safe, ergonomic API.

use std::os::raw::{c_char, c_int};

#[repr(C)]
#[derive(Clone, Copy)]
pub struct LhtHeader {
    pub name: *const c_char,
    pub name_len: usize,
    pub value: *const c_char,
    pub value_len: usize,
}

extern "C" {
    pub fn lht_parse_request(
        buf: *const c_char,
        len: usize,
        method: *mut *const c_char,
        method_len: *mut usize,
        path: *mut *const c_char,
        path_len: *mut usize,
        minor_version: *mut c_int,
        headers: *mut LhtHeader,
        num_headers: *mut usize,
    ) -> c_int;

    pub fn lht_parse_response(
        buf: *const c_char,
        len: usize,
        status_code: *mut c_int,
        reason: *mut *const c_char,
        reason_len: *mut usize,
        minor_version: *mut c_int,
        headers: *mut LhtHeader,
        num_headers: *mut usize,
    ) -> c_int;

    pub fn lht_simd_backend() -> *const c_char;
    pub fn lht_has_sse42() -> c_int;
    pub fn lht_has_avx2() -> c_int;
}

pub mod safe;
