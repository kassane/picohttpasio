#pragma once

#include "http_types.hpp"
#include "picohttpwrapper.hpp"
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace pico {

// ---------------------------------------------------------------------------
// Request  — rich value-type passed to route handlers
// ---------------------------------------------------------------------------
struct Request {
    Method      method      = Method::UNKNOWN;
    std::string path;          // decoded path (no query string)
    std::string query;         // raw query string (after '?')
    int         httpVersion = 1; // minor version: 0 = HTTP/1.0, 1 = HTTP/1.1
    std::string body;

    std::map<std::string, std::string> headers;
    std::map<std::string, std::string> pathParams;   // populated by Router
    std::map<std::string, std::string> queryParams;  // parsed from query string

    // Accessor helpers (case-sensitive — HTTP/1.1 headers are case-insensitive
    // by spec, but picohttpparser preserves original case; callers should use
    // canonical form, e.g. "Content-Type").
    std::string_view header(std::string_view name) const {
        auto it = headers.find(std::string(name));
        return (it != headers.end()) ? std::string_view(it->second) : std::string_view{};
    }

    // Path parameter populated by the Router when a :name segment matches.
    std::string_view param(std::string_view name) const {
        auto it = pathParams.find(std::string(name));
        return (it != pathParams.end()) ? std::string_view(it->second) : std::string_view{};
    }

    // Query-string parameter (decoded).
    std::string_view query_param(std::string_view name) const {
        auto it = queryParams.find(std::string(name));
        return (it != queryParams.end()) ? std::string_view(it->second) : std::string_view{};
    }

    // True if the client requested (or HTTP/1.1 defaults to) keep-alive.
    bool keepAlive() const {
        auto it = headers.find("Connection");
        if (it != headers.end()) {
            std::string_view v = it->second;
            if (v == "close") return false;
            if (v == "keep-alive" || v == "Keep-Alive") return true;
        }
        // HTTP/1.1 defaults to keep-alive; HTTP/1.0 defaults to close.
        return (httpVersion >= 1);
    }
};

// ---------------------------------------------------------------------------
// Factory: parse raw HTTP request bytes into a Request
// body is the bytes already read after the header block (may be empty)
// ---------------------------------------------------------------------------
inline std::optional<Request> parse_request(std::string_view raw,
                                             std::string body = {}) {
    HTTPRequest parser(raw);
    if (!parser.parse()) return std::nullopt;

    Request req;
    req.method      = method_from_string(parser.getMethod());
    req.path        = parser.getPath();
    req.query       = parser.getQuery();
    req.httpVersion = parser.getMinorVersion();
    req.body        = std::move(body);
    req.headers     = parser.getHeaders();
    req.queryParams = parser.queryParams();
    return req;
}

} // namespace pico
