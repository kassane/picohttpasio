#pragma once

#include <string_view>
#include <cstdint>

namespace pico {

// ---------------------------------------------------------------------------
// HTTP Method
// ---------------------------------------------------------------------------

enum class Method : uint8_t {
    GET,
    POST,
    PUT,
    DEL,      // HTTP DELETE — named DEL to avoid collision with winnt.h #define DELETE
    PATCH,
    HEAD,
    OPTIONS,
    UNKNOWN
};

inline Method method_from_string(std::string_view s) noexcept {
    if (s == "GET")     return Method::GET;
    if (s == "POST")    return Method::POST;
    if (s == "PUT")     return Method::PUT;
    if (s == "DELETE")  return Method::DEL;
    if (s == "PATCH")   return Method::PATCH;
    if (s == "HEAD")    return Method::HEAD;
    if (s == "OPTIONS") return Method::OPTIONS;
    return Method::UNKNOWN;
}

inline std::string_view method_to_string(Method m) noexcept {
    switch (m) {
        case Method::GET:     return "GET";
        case Method::POST:    return "POST";
        case Method::PUT:     return "PUT";
        case Method::DEL:     return "DELETE";
        case Method::PATCH:   return "PATCH";
        case Method::HEAD:    return "HEAD";
        case Method::OPTIONS: return "OPTIONS";
        default:              return "UNKNOWN";
    }
}

// ---------------------------------------------------------------------------
// HTTP Status Codes
// ---------------------------------------------------------------------------

enum class StatusCode : unsigned {
    Continue            = 100,
    SwitchingProtocols  = 101,
    OK                  = 200,
    Created             = 201,
    Accepted            = 202,
    NoContent           = 204,
    MovedPermanently    = 301,
    Found               = 302,
    NotModified         = 304,
    BadRequest          = 400,
    Unauthorized        = 401,
    Forbidden           = 403,
    NotFound            = 404,
    MethodNotAllowed    = 405,
    Conflict            = 409,
    UnprocessableEntity = 422,
    TooManyRequests     = 429,
    InternalServerError = 500,
    NotImplemented      = 501,
    BadGateway          = 502,
    ServiceUnavailable  = 503,
};

inline std::string_view status_message(StatusCode c) noexcept {
    switch (c) {
        case StatusCode::Continue:            return "Continue";
        case StatusCode::SwitchingProtocols:  return "Switching Protocols";
        case StatusCode::OK:                  return "OK";
        case StatusCode::Created:             return "Created";
        case StatusCode::Accepted:            return "Accepted";
        case StatusCode::NoContent:           return "No Content";
        case StatusCode::MovedPermanently:    return "Moved Permanently";
        case StatusCode::Found:               return "Found";
        case StatusCode::NotModified:         return "Not Modified";
        case StatusCode::BadRequest:          return "Bad Request";
        case StatusCode::Unauthorized:        return "Unauthorized";
        case StatusCode::Forbidden:           return "Forbidden";
        case StatusCode::NotFound:            return "Not Found";
        case StatusCode::MethodNotAllowed:    return "Method Not Allowed";
        case StatusCode::Conflict:            return "Conflict";
        case StatusCode::UnprocessableEntity: return "Unprocessable Entity";
        case StatusCode::TooManyRequests:     return "Too Many Requests";
        case StatusCode::InternalServerError: return "Internal Server Error";
        case StatusCode::NotImplemented:      return "Not Implemented";
        case StatusCode::BadGateway:          return "Bad Gateway";
        case StatusCode::ServiceUnavailable:  return "Service Unavailable";
        default:                              return "Unknown";
    }
}

inline bool status_is_success(StatusCode c) noexcept {
    auto v = static_cast<unsigned>(c);
    return v >= 200 && v < 300;
}

inline bool status_is_redirect(StatusCode c) noexcept {
    auto v = static_cast<unsigned>(c);
    return v >= 300 && v < 400;
}

inline bool status_is_client_error(StatusCode c) noexcept {
    auto v = static_cast<unsigned>(c);
    return v >= 400 && v < 500;
}

inline bool status_is_server_error(StatusCode c) noexcept {
    auto v = static_cast<unsigned>(c);
    return v >= 500 && v < 600;
}

} // namespace pico
