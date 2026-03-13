#pragma once

#include "http_types.hpp"
#include <map>
#include <string>
#include <string_view>

namespace pico {

// ---------------------------------------------------------------------------
// Response — fluent builder + wire serializer
// ---------------------------------------------------------------------------
class Response {
public:
    // ---- Construction helpers -------------------------------------------

    static Response make(StatusCode code = StatusCode::OK) {
        Response r;
        r.status_code_ = code;
        return r;
    }

    static Response ok(std::string b = {}) {
        return make(StatusCode::OK).body(std::move(b));
    }

    static Response created(std::string b = {}) {
        return make(StatusCode::Created).body(std::move(b));
    }

    static Response no_content() {
        return make(StatusCode::NoContent);
    }

    static Response not_found(std::string msg = "Not Found") {
        return make(StatusCode::NotFound).body(std::move(msg));
    }

    static Response bad_request(std::string msg = "Bad Request") {
        return make(StatusCode::BadRequest).body(std::move(msg));
    }

    static Response method_not_allowed() {
        return make(StatusCode::MethodNotAllowed).body("Method Not Allowed");
    }

    static Response internal_error(std::string msg = "Internal Server Error") {
        return make(StatusCode::InternalServerError).body(std::move(msg));
    }

    // Used for WebSocket upgrade (caller must pass the computed accept key)
    static Response switching_protocols(std::string_view ws_accept_key) {
        return make(StatusCode::SwitchingProtocols)
            .header("Upgrade",    "websocket")
            .header("Connection", "Upgrade")
            .header("Sec-WebSocket-Accept", std::string(ws_accept_key));
    }

    static Response redirect(std::string_view location,
                              StatusCode code = StatusCode::Found) {
        return make(code).header("Location", std::string(location));
    }

    // ---- Fluent setters -------------------------------------------------

    Response& status(StatusCode code) {
        status_code_ = code;
        return *this;
    }

    Response& header(std::string name, std::string value) {
        headers_[std::move(name)] = std::move(value);
        return *this;
    }

    Response& body(std::string data,
                   std::string_view content_type = "text/plain; charset=utf-8") {
        body_ = std::move(data);
        if (!body_.empty())
            headers_["Content-Type"] = std::string(content_type);
        return *this;
    }

    Response& json(std::string json_str) {
        return body(std::move(json_str), "application/json");
    }

    Response& html(std::string html_str) {
        return body(std::move(html_str), "text/html; charset=utf-8");
    }

    // ---- Accessors ------------------------------------------------------

    StatusCode                              statusCode()    const { return status_code_; }
    const std::string&                      getBody()       const { return body_; }
    const std::map<std::string,std::string>& getHeaders()   const { return headers_; }

    // ---- Serialization --------------------------------------------------

    // Returns a complete HTTP/1.1 response ready to send over the wire.
    std::string serialize() const {
        auto code_val = static_cast<unsigned>(status_code_);
        auto msg      = status_message(status_code_);

        std::string out;
        out.reserve(256 + body_.size());
        out  = "HTTP/1.1 ";
        out += std::to_string(code_val);
        out += ' ';
        out += msg;
        out += "\r\n";

        // Always emit Content-Length (even 0) so clients know when body ends
        out += "Content-Length: ";
        out += std::to_string(body_.size());
        out += "\r\n";

        for (const auto& [k, v] : headers_) {
            out += k;
            out += ": ";
            out += v;
            out += "\r\n";
        }
        out += "\r\n";
        out += body_;
        return out;
    }

private:
    StatusCode  status_code_ = StatusCode::OK;
    std::string body_;
    std::map<std::string, std::string> headers_;
};

} // namespace pico
