#!/usr/bin/env python3
"""Example: parse HTTP requests/responses with lightning."""
import lightning

print(f"SIMD backend: {lightning.simd_backend()}")
print(f"SSE4.2: {lightning.has_sse42()}  AVX2: {lightning.has_avx2()}")
print()

req_data = (
    b"GET /api/users?id=42&format=json HTTP/1.1\r\n"
    b"Host: example.com\r\n"
    b"User-Agent: lightning-test/1.0\r\n"
    b"Accept: application/json\r\n"
    b"Connection: keep-alive\r\n"
    b"\r\n"
)

req = lightning.parse_request(req_data)
print("=== Request ===")
print(f"Method:  {req.method.decode()}")
print(f"Path:    {req.path.decode()}")
print(f"Version: HTTP/1.{req.minor_version}")
print(f"Headers ({len(req.headers)}):")
for h in req.headers:
    print(f"  {h.name.decode()}: {h.value.decode()}")
print(f"Host via lookup: {req.header('Host').decode()}")
print()

resp_data = (
    b"HTTP/1.1 200 OK\r\n"
    b"Content-Type: application/json\r\n"
    b"Content-Length: 25\r\n"
    b"Server: lightning/0.1\r\n"
    b"\r\n"
    b'{"status":"ok","code":200}'
)

resp = lightning.parse_response(resp_data)
print("=== Response ===")
print(f"Status:  {resp.status_code}")
print(f"Reason:  {resp.reason.decode()}")
print(f"Version: HTTP/1.{resp.minor_version}")
print(f"Headers ({len(resp.headers)}):")
for h in resp.headers:
    print(f"  {h.name.decode()}: {h.value.decode()}")
body = resp_data[resp.body_offset:]
print(f"Body:    {body.decode()}")
