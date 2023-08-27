
#ifndef picohttpwrapper_hpp
#define picohttpwrapper_hpp

#include <string>
#include <cstring>
#include <map>
#include "picohttpparser.h"

class HTTPRequest {
public:
    HTTPRequest(const std::string& request) : request_(request) {}

    bool parse() {
        const char* method;
        size_t method_len;
        const char* path;
        size_t path_len;
        int minor_version;
        struct phr_header headers[100];
        size_t num_headers = sizeof(headers) / sizeof(headers[0]);
        int pret = phr_parse_request(request_.c_str(), request_.size(),
                                     &method, &method_len, &path, &path_len,
                                     &minor_version, headers, &num_headers, 0);

        if (pret > 0) {
            method_ = std::string(method, method_len);
            path_ = std::string(path, path_len);
            minor_version_ = minor_version;

            for (size_t i = 0; i < num_headers; ++i) {
                std::string header_name(headers[i].name, headers[i].name_len);
                std::string header_value(headers[i].value, headers[i].value_len);
                headers_[header_name] = header_value;
            }
            return true;
        }

        return false;
    }

    const std::string& getMethod() const {
        return method_;
    }

    const std::string& getPath() const {
        return path_;
    }

    int getMinorVersion() const {
        return minor_version_;
    }

    const std::string& getHeader(const std::string& name) const {
        return headers_.at(name);
    }

private:
    std::string request_;
    std::string method_;
    std::string path_;
    int minor_version_;
    std::map<std::string, std::string> headers_;
};
#endif
