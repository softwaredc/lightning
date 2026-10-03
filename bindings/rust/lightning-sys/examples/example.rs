//! Example: parse HTTP with lightning-sys.
//!
//! Run: cargo run --example example

use lightning_sys::safe;

fn main() {
    let req_data = b"GET /api/hello?name=world HTTP/1.1\r\nHost: test.com\r\nAccept: */*\r\n\r\n";
    let req = safe::parse_request(req_data).unwrap();
    println!("Method:  {}", String::from_utf8_lossy(&req.method));
    println!("Path:    {}", String::from_utf8_lossy(&req.path));
    println!("Version: HTTP/1.{}", req.minor_version);
    println!("Headers ({}):", req.headers.len());
    for h in &req.headers {
        println!("  {}: {}", String::from_utf8_lossy(&h.name), String::from_utf8_lossy(&h.value));
    }

    println!();
    let resp_data = b"HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 5\r\n\r\nhello";
    let resp = safe::parse_response(resp_data).unwrap();
    println!("Status:  {}", resp.status_code);
    println!("Reason: {}", String::from_utf8_lossy(&resp.reason));
    println!("Body:   {}", String::from_utf8_lossy(&resp_data[resp.body_offset..]));

    println!();
    println!("SIMD backend: {}", safe::simd_backend());
}
