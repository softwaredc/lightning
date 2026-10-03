//! Safe wrapper around lightning-sys.
//!
//! # Example
//! ```no_run
//! use lightning_sys::safe::parse_request;
//!
//! let data = b"GET /hello HTTP/1.1\r\nHost: x\r\n\r\n";
//! let req = parse_request(data).unwrap();
//! println!("{}", String::from_utf8_lossy(&req.method));
//! ```

use std::ffi::CStr;
use std::os::raw::{c_char, c_int};
use std::ptr;

use crate as sys;

pub struct Header {
    pub name: Vec<u8>,
    pub value: Vec<u8>,
}

pub struct Request {
    pub method: Vec<u8>,
    pub path: Vec<u8>,
    pub minor_version: i32,
    pub headers: Vec<Header>,
    pub body_offset: usize,
}

pub struct Response {
    pub status_code: i32,
    pub reason: Vec<u8>,
    pub minor_version: i32,
    pub headers: Vec<Header>,
    pub body_offset: usize,
}

const MAX_HEADERS: usize = 256;

fn err_str(code: i32) -> &'static str {
    match code {
        -1 => "need more data",
        -2 => "malformed input",
        -3 => "too many headers",
        -4 => "invalid argument",
        -5 => "feature not compiled in",
        _ => "unknown error",
    }
}

unsafe fn cstr_to_vec(ptr: *const c_char, len: usize) -> Vec<u8> {
    if ptr.is_null() || len == 0 {
        return Vec::new();
    }
    let slice = std::slice::from_raw_parts(ptr as *const u8, len);
    slice.to_vec()
}

pub fn parse_request(buf: &[u8]) -> Result<Request, &'static str> {
    let mut method: *const c_char = ptr::null();
    let mut method_len: usize = 0;
    let mut path: *const c_char = ptr::null();
    let mut path_len: usize = 0;
    let mut minor: c_int = 0;
    let mut headers = [sys::LhtHeader {
        name: ptr::null(),
        name_len: 0,
        value: ptr::null(),
        value_len: 0,
    }; MAX_HEADERS];
    let mut num: usize = MAX_HEADERS;

    let ret = unsafe {
        sys::lht_parse_request(
            buf.as_ptr() as *const c_char,
            buf.len(),
            &mut method,
            &mut method_len,
            &mut path,
            &mut path_len,
            &mut minor,
            headers.as_mut_ptr(),
            &mut num,
        )
    };
    if ret < 0 {
        return Err(err_str(ret));
    }

    let mut hs = Vec::with_capacity(num);
    for i in 0..num {
        unsafe {
            hs.push(Header {
                name: cstr_to_vec(headers[i].name, headers[i].name_len),
                value: cstr_to_vec(headers[i].value, headers[i].value_len),
            });
        }
    }

    Ok(Request {
        method: unsafe { cstr_to_vec(method, method_len) },
        path: unsafe { cstr_to_vec(path, path_len) },
        minor_version: minor,
        headers: hs,
        body_offset: ret as usize,
    })
}

pub fn parse_response(buf: &[u8]) -> Result<Response, &'static str> {
    let mut status: c_int = 0;
    let mut reason: *const c_char = ptr::null();
    let mut reason_len: usize = 0;
    let mut minor: c_int = 0;
    let mut headers = [sys::LhtHeader {
        name: ptr::null(),
        name_len: 0,
        value: ptr::null(),
        value_len: 0,
    }; MAX_HEADERS];
    let mut num: usize = MAX_HEADERS;

    let ret = unsafe {
        sys::lht_parse_response(
            buf.as_ptr() as *const c_char,
            buf.len(),
            &mut status,
            &mut reason,
            &mut reason_len,
            &mut minor,
            headers.as_mut_ptr(),
            &mut num,
        )
    };
    if ret < 0 {
        return Err(err_str(ret));
    }

    let mut hs = Vec::with_capacity(num);
    for i in 0..num {
        unsafe {
            hs.push(Header {
                name: cstr_to_vec(headers[i].name, headers[i].name_len),
                value: cstr_to_vec(headers[i].value, headers[i].value_len),
            });
        }
    }

    Ok(Response {
        status_code: status,
        reason: unsafe { cstr_to_vec(reason, reason_len) },
        minor_version: minor,
        headers: hs,
        body_offset: ret as usize,
    })
}

pub fn simd_backend() -> String {
    unsafe {
        let ptr = sys::lht_simd_backend();
        CStr::from_ptr(ptr).to_string_lossy().into_owned()
    }
}
