// lightning - C++ example: parse HTTP request/response with RAII wrapper.
// SPDX-License-Identifier: Apache-2.0
#include <lightning/lightning.h>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace lht {

struct Header {
    std::string_view name;
    std::string_view value;
};

struct Request {
    std::string_view method;
    std::string_view path;
    int minor_version;
    std::vector<Header> headers;
    int body_offset;
};

struct Response {
    int status_code;
    std::string_view reason;
    int minor_version;
    std::vector<Header> headers;
    int body_offset;
};

Request parse_request(std::string_view buf) {
    const char *method = nullptr; size_t method_len = 0;
    const char *path = nullptr;   size_t path_len = 0;
    int minor = 0;
    lht_header_t hdrs[256];
    size_t n = 256;

    int ret = lht_parse_request(buf.data(), buf.size(),
        &method, &method_len, &path, &path_len,
        &minor, hdrs, &n);
    if (ret < 0)
        throw std::runtime_error("parse error: " + std::to_string(ret));

    Request req;
    req.method = {method, method_len};
    req.path = {path, path_len};
    req.minor_version = minor;
    req.body_offset = ret;
    for (size_t i = 0; i < n; i++)
        req.headers.push_back({{hdrs[i].name, hdrs[i].name_len},
                               {hdrs[i].value, hdrs[i].value_len}});
    return req;
}

Response parse_response(std::string_view buf) {
    int status = 0;
    const char *reason = nullptr; size_t reason_len = 0;
    int minor = 0;
    lht_header_t hdrs[256];
    size_t n = 256;

    int ret = lht_parse_response(buf.data(), buf.size(),
        &status, &reason, &reason_len, &minor, hdrs, &n);
    if (ret < 0)
        throw std::runtime_error("parse error: " + std::to_string(ret));

    Response resp;
    resp.status_code = status;
    resp.reason = {reason, reason_len};
    resp.minor_version = minor;
    resp.body_offset = ret;
    for (size_t i = 0; i < n; i++)
        resp.headers.push_back({{hdrs[i].name, hdrs[i].name_len},
                                {hdrs[i].value, hdrs[i].value_len}});
    return resp;
}

} // namespace lht

int main() {
    std::cout << "SIMD backend: " << lht_simd_backend() << "\n\n";

    const char *req_str =
        "GET /api/users?id=42 HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "Accept: application/json\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";

    auto req = lht::parse_request({req_str, std::strlen(req_str)});
    std::cout << "=== Request ===\n";
    std::cout << "Method:  " << req.method << "\n";
    std::cout << "Path:    " << req.path << "\n";
    std::cout << "Version: HTTP/1." << req.minor_version << "\n";
    std::cout << "Headers (" << req.headers.size() << "):\n";
    for (const auto &h : req.headers)
        std::cout << "  " << h.name << ": " << h.value << "\n";

    std::cout << "\n";

    const char *resp_str =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 25\r\n"
        "Server: lightning/0.1\r\n"
        "\r\n"
        R"({"status":"ok","code":200})";

    auto resp = lht::parse_response({resp_str, std::strlen(resp_str)});
    std::cout << "=== Response ===\n";
    std::cout << "Status:  " << resp.status_code << "\n";
    std::cout << "Reason:  " << resp.reason << "\n";
    std::cout << "Headers (" << resp.headers.size() << "):\n";
    for (const auto &h : resp.headers)
        std::cout << "  " << h.name << ": " << h.value << "\n";
    std::string_view body = {resp_str + resp.body_offset,
                              std::strlen(resp_str) - resp.body_offset};
    std::cout << "Body:    " << body << "\n";
}
