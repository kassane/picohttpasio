#pragma once

#include "http_types.hpp"
#include "picohttpwrapper.hpp"
#include <asio.hpp>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#ifdef PICO_ENABLE_TLS
#include "ssl_context.hpp"
#include <asio/ssl.hpp>
#endif

using asio::ip::tcp;

namespace pico {

// ---------------------------------------------------------------------------
// RequestBuilder — fluent HTTP request serializer
// ---------------------------------------------------------------------------
class RequestBuilder {
public:
    RequestBuilder& method(Method m) {
        method_ = m;
        return *this;
    }

    RequestBuilder& get (std::string_view path) { method_ = Method::GET;    path_ = path; return *this; }
    RequestBuilder& post(std::string_view path) { method_ = Method::POST;   path_ = path; return *this; }
    RequestBuilder& put (std::string_view path) { method_ = Method::PUT;    path_ = path; return *this; }
    RequestBuilder& del (std::string_view path) { method_ = Method::DEL;    path_ = path; return *this; }

    RequestBuilder& path(std::string_view p)    { path_ = p;    return *this; }
    RequestBuilder& host(std::string_view h)    { host_ = h;    return *this; }

    RequestBuilder& header(std::string name, std::string value) {
        headers_.emplace_back(std::move(name), std::move(value));
        return *this;
    }

    RequestBuilder& body(std::string data,
                         std::string_view content_type = "text/plain; charset=utf-8") {
        body_         = std::move(data);
        content_type_ = std::string(content_type);
        return *this;
    }

    RequestBuilder& json(std::string data) {
        return body(std::move(data), "application/json");
    }

    RequestBuilder& keep_alive(bool v = true) { keep_alive_ = v; return *this; }

    // Serialize to wire-format HTTP/1.1 request string.
    std::string build() const {
        std::string out;
        out  = std::string(method_to_string(method_));
        out += ' ';
        out += path_.empty() ? "/" : path_;
        out += " HTTP/1.1\r\n";

        if (!host_.empty()) {
            out += "Host: ";
            out += host_;
            out += "\r\n";
        }

        out += "Connection: ";
        out += keep_alive_ ? "keep-alive" : "close";
        out += "\r\n";

        if (!body_.empty()) {
            out += "Content-Type: ";
            out += content_type_;
            out += "\r\n";
            out += "Content-Length: ";
            out += std::to_string(body_.size());
            out += "\r\n";
        }

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
    Method      method_       = Method::GET;
    std::string path_         = "/";
    std::string host_;
    std::string body_;
    std::string content_type_ = "text/plain; charset=utf-8";
    bool        keep_alive_   = false;
    std::vector<std::pair<std::string,std::string>> headers_;
};

// ---------------------------------------------------------------------------
// HTTPClient — async HTTP/1.1 client with keep-alive support
//
// Usage:
//   HTTPClient client(io, "example.com", 80);
//   auto req = RequestBuilder{}.get("/").host("example.com").build();
//   client.request(req, [](auto opt_resp) { ... });
// ---------------------------------------------------------------------------
class HTTPClient {
public:
    using ResponseCallback = std::function<void(std::optional<HTTPResponse>)>;

    HTTPClient(asio::io_context& io,
               std::string host,
               unsigned short port)
        : resolver_(io)
        , socket_(io)
        , host_(std::move(host))
        , port_(port) {}

    // Send a pre-built request string; callback receives parsed response (or nullopt on error).
    // If the connection is already open (keep-alive), skips re-connection.
    void request(std::string wire_request, ResponseCallback cb) {
        pending_request_  = std::move(wire_request);
        pending_callback_ = std::move(cb);

        if (connected_) {
            sendRequest();
        } else {
            connect();
        }
    }

    // Convenience: build and send a GET request.
    void get(std::string_view path, ResponseCallback cb) {
        auto req = RequestBuilder{}.get(path).host(host_).keep_alive(true).build();
        request(std::move(req), std::move(cb));
    }

private:
    void connect() {
        resolver_.async_resolve(
            host_, std::to_string(port_),
            [this](std::error_code ec, tcp::resolver::results_type endpoints) {
                if (ec) { invoke_callback(std::nullopt); return; }
                asio::async_connect(socket_, endpoints,
                    [this](std::error_code ec, tcp::endpoint) {
                        if (ec) { invoke_callback(std::nullopt); return; }
                        connected_ = true;
                        sendRequest();
                    });
            });
    }

    void sendRequest() {
        auto wire = std::make_shared<std::string>(std::move(pending_request_));
        asio::async_write(socket_, asio::buffer(*wire),
            [this, wire](std::error_code ec, size_t) {
                if (ec) { connected_ = false; invoke_callback(std::nullopt); return; }
                readResponseHeaders();
            });
    }

    void readResponseHeaders() {
        asio::async_read_until(
            socket_, asio::dynamic_buffer(read_buf_), "\r\n\r\n",
            [this](std::error_code ec, size_t header_bytes) {
                if (ec) { connected_ = false; invoke_callback(std::nullopt); return; }
                processResponse(header_bytes);
            });
    }

    void processResponse(size_t header_bytes) {
        std::string header_str = read_buf_.substr(0, header_bytes);
        read_buf_.erase(0, header_bytes);

        HTTPResponse parser(header_str);
        if (!parser.parse()) {
            invoke_callback(std::nullopt);
            return;
        }

        size_t content_len = 0;
        const auto& cl = parser.header("Content-Length");
        if (!cl.empty()) {
            try { content_len = static_cast<size_t>(std::stoul(cl)); }
            catch (...) {}
        }

        if (content_len == 0) {
            const auto& conn = parser.header("Connection");
            if (conn == "close") connected_ = false;
            invoke_callback(parser);
            return;
        }

        // Read body
        if (read_buf_.size() >= content_len) {
            const auto& conn = parser.header("Connection");
            if (conn == "close") connected_ = false;
            invoke_callback(parser);
            read_buf_.erase(0, content_len);
        } else {
            size_t remaining = content_len - read_buf_.size();
            asio::async_read(socket_, asio::dynamic_buffer(read_buf_),
                asio::transfer_exactly(remaining),
                [this, parser = std::move(parser), content_len](std::error_code ec, size_t) mutable {
                    if (ec) { connected_ = false; invoke_callback(std::nullopt); return; }
                    const auto& conn = parser.header("Connection");
                    if (conn == "close") connected_ = false;
                    invoke_callback(parser);
                    read_buf_.erase(0, content_len);
                });
        }
    }

    void invoke_callback(std::optional<HTTPResponse> resp) {
        if (pending_callback_) {
            auto cb = std::move(pending_callback_);
            pending_callback_ = nullptr;
            cb(std::move(resp));
        }
    }

    tcp::resolver  resolver_;
    tcp::socket    socket_;
    std::string    host_;
    unsigned short port_;
    bool           connected_ = false;

    std::string      read_buf_;
    std::string      pending_request_;
    ResponseCallback pending_callback_;
};

// ---------------------------------------------------------------------------
// HTTPSClient — async HTTPS/1.1 client with TLS support
// (requires PICO_ENABLE_TLS)
//
// Usage:
//   auto ctx = pico::ssl::make_client_context();   // or make_client_context(false) for self-signed
//   pico::HTTPSClient client(io, "example.com", 443, ctx);
//   client.get("/api", [](auto opt_resp) { ... });
// ---------------------------------------------------------------------------
#ifdef PICO_ENABLE_TLS
class HTTPSClient {
public:
    using ResponseCallback = std::function<void(std::optional<HTTPResponse>)>;

    HTTPSClient(asio::io_context& io,
                std::string host,
                unsigned short port,
                asio::ssl::context& ctx)
        : resolver_(io)
        , socket_(io, ctx)
        , host_(std::move(host))
        , port_(port) {}

    // Send a pre-built request string; callback receives parsed response (or nullopt on error).
    void request(std::string wire_request, ResponseCallback cb) {
        pending_request_  = std::move(wire_request);
        pending_callback_ = std::move(cb);

        if (connected_) {
            sendRequest();
        } else {
            connect();
        }
    }

    // Convenience: build and send a GET request.
    void get(std::string_view path, ResponseCallback cb) {
        auto req = RequestBuilder{}.get(path).host(host_).keep_alive(true).build();
        request(std::move(req), std::move(cb));
    }

private:
    void connect() {
        resolver_.async_resolve(
            host_, std::to_string(port_),
            [this](std::error_code ec, tcp::resolver::results_type endpoints) {
                if (ec) { invoke_callback(std::nullopt); return; }
                asio::async_connect(socket_.lowest_layer(), endpoints,
                    [this](std::error_code ec, tcp::endpoint) {
                        if (ec) { invoke_callback(std::nullopt); return; }
                        // SSL handshake after TCP connect
                        socket_.async_handshake(
                            asio::ssl::stream_base::client,
                            [this](std::error_code ec) {
                                if (ec) { invoke_callback(std::nullopt); return; }
                                connected_ = true;
                                sendRequest();
                            });
                    });
            });
    }

    void sendRequest() {
        auto wire = std::make_shared<std::string>(std::move(pending_request_));
        asio::async_write(socket_, asio::buffer(*wire),
            [this, wire](std::error_code ec, size_t) {
                if (ec) { connected_ = false; invoke_callback(std::nullopt); return; }
                readResponseHeaders();
            });
    }

    void readResponseHeaders() {
        asio::async_read_until(
            socket_, asio::dynamic_buffer(read_buf_), "\r\n\r\n",
            [this](std::error_code ec, size_t header_bytes) {
                if (ec) { connected_ = false; invoke_callback(std::nullopt); return; }
                processResponse(header_bytes);
            });
    }

    void processResponse(size_t header_bytes) {
        std::string header_str = read_buf_.substr(0, header_bytes);
        read_buf_.erase(0, header_bytes);

        HTTPResponse parser(header_str);
        if (!parser.parse()) {
            invoke_callback(std::nullopt);
            return;
        }

        size_t content_len = 0;
        const auto& cl = parser.header("Content-Length");
        if (!cl.empty()) {
            try { content_len = static_cast<size_t>(std::stoul(cl)); }
            catch (...) {}
        }

        if (content_len == 0) {
            const auto& conn = parser.header("Connection");
            if (conn == "close") connected_ = false;
            invoke_callback(parser);
            return;
        }

        if (read_buf_.size() >= content_len) {
            const auto& conn = parser.header("Connection");
            if (conn == "close") connected_ = false;
            invoke_callback(parser);
            read_buf_.erase(0, content_len);
        } else {
            size_t remaining = content_len - read_buf_.size();
            asio::async_read(socket_, asio::dynamic_buffer(read_buf_),
                asio::transfer_exactly(remaining),
                [this, parser = std::move(parser), content_len](std::error_code ec, size_t) mutable {
                    if (ec) { connected_ = false; invoke_callback(std::nullopt); return; }
                    const auto& conn = parser.header("Connection");
                    if (conn == "close") connected_ = false;
                    invoke_callback(parser);
                    read_buf_.erase(0, content_len);
                });
        }
    }

    void invoke_callback(std::optional<HTTPResponse> resp) {
        if (pending_callback_) {
            auto cb = std::move(pending_callback_);
            pending_callback_ = nullptr;
            cb(std::move(resp));
        }
    }

    tcp::resolver                       resolver_;
    asio::ssl::stream<tcp::socket>      socket_;
    std::string                         host_;
    unsigned short                      port_;
    bool                                connected_ = false;

    std::string      read_buf_;
    std::string      pending_request_;
    ResponseCallback pending_callback_;
};
#endif // PICO_ENABLE_TLS

} // namespace pico
