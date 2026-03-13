#pragma once

#include "picohttpparser.h"
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <string_view>

// ---------------------------------------------------------------------------
// HTTPRequest — wraps phr_parse_request
// ---------------------------------------------------------------------------
class HTTPRequest {
public:
    explicit HTTPRequest(std::string_view request) : request_(request) {}

    bool parse() {
        const char *method = nullptr;
        size_t method_len  = 0;
        const char *path   = nullptr;
        size_t path_len    = 0;
        int minor_version  = 0;
        struct phr_header headers[100];
        size_t num_headers = sizeof(headers) / sizeof(headers[0]);

        int pret = phr_parse_request(
            request_.data(), request_.size(),
            &method, &method_len,
            &path,   &path_len,
            &minor_version,
            headers, &num_headers, 0);

        if (pret <= 0) return false;

        method_        = std::string(method, method_len);
        minor_version_ = minor_version;

        // Split path and query string
        std::string_view full_path(path, path_len);
        auto qpos = full_path.find('?');
        if (qpos == std::string_view::npos) {
            path_  = std::string(full_path);
            query_ = {};
        } else {
            path_  = std::string(full_path.substr(0, qpos));
            query_ = std::string(full_path.substr(qpos + 1));
        }

        headers_.clear();
        for (size_t i = 0; i < num_headers; ++i) {
            headers_[std::string(headers[i].name, headers[i].name_len)] =
                std::string(headers[i].value, headers[i].value_len);
        }
        return true;
    }

    const std::string &getMethod()       const { return method_; }
    const std::string &getPath()         const { return path_; }
    const std::string &getQuery()        const { return query_; }
    int                getMinorVersion() const { return minor_version_; }

    const std::string &getHeader(std::string_view name) const {
        auto it = headers_.find(std::string(name));
        if (it != headers_.end()) return it->second;
        static const std::string empty;
        return empty;
    }

    const std::map<std::string, std::string> &getHeaders() const { return headers_; }

    // Returns decoded key→value pairs from the query string
    std::map<std::string, std::string> queryParams() const {
        return parse_query(query_);
    }

    static std::map<std::string, std::string> parse_query(std::string_view qs) {
        std::map<std::string, std::string> out;
        while (!qs.empty()) {
            auto amp = qs.find('&');
            auto pair = (amp == std::string_view::npos) ? qs : qs.substr(0, amp);
            auto eq = pair.find('=');
            if (eq != std::string_view::npos) {
                out[url_decode(pair.substr(0, eq))] = url_decode(pair.substr(eq + 1));
            } else if (!pair.empty()) {
                out[url_decode(pair)] = {};
            }
            qs = (amp == std::string_view::npos) ? std::string_view{} : qs.substr(amp + 1);
        }
        return out;
    }

    static std::string url_decode(std::string_view s) {
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '+') {
                out += ' ';
            } else if (s[i] == '%' && i + 2 < s.size()) {
                char hex[3] = { s[i+1], s[i+2], '\0' };
                out += static_cast<char>(strtol(hex, nullptr, 16));
                i += 2;
            } else {
                out += s[i];
            }
        }
        return out;
    }

private:
    std::string request_;
    std::string method_;
    std::string path_;
    std::string query_;
    int         minor_version_ = 0;
    std::map<std::string, std::string> headers_;
};

// ---------------------------------------------------------------------------
// HTTPResponse — wraps phr_parse_response
// ---------------------------------------------------------------------------
class HTTPResponse {
public:
    explicit HTTPResponse(std::string_view raw) : raw_(raw) {}

    bool parse() {
        int minor_version = 0;
        int status        = 0;
        const char *msg   = nullptr;
        size_t msg_len    = 0;
        struct phr_header headers[100];
        size_t num_headers = sizeof(headers) / sizeof(headers[0]);

        int pret = phr_parse_response(
            raw_.data(), raw_.size(),
            &minor_version, &status,
            &msg, &msg_len,
            headers, &num_headers, 0);

        if (pret <= 0) return false;

        minor_version_  = minor_version;
        status_code_    = status;
        status_message_ = std::string(msg, msg_len);

        headers_.clear();
        for (size_t i = 0; i < num_headers; ++i) {
            headers_[std::string(headers[i].name, headers[i].name_len)] =
                std::string(headers[i].value, headers[i].value_len);
        }
        return true;
    }

    int                statusCode()    const { return status_code_; }
    const std::string &statusMessage() const { return status_message_; }
    int                minorVersion()  const { return minor_version_; }

    const std::string &header(std::string_view name) const {
        auto it = headers_.find(std::string(name));
        if (it != headers_.end()) return it->second;
        static const std::string empty;
        return empty;
    }

    const std::map<std::string, std::string> &headers() const { return headers_; }

private:
    std::string raw_;
    int         minor_version_ = 0;
    int         status_code_   = 0;
    std::string status_message_;
    std::map<std::string, std::string> headers_;
};
