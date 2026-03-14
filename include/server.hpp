#pragma once

// ---------------------------------------------------------------------------
// server.hpp — callback-based HTTP (and HTTPS) server
//
// Plain HTTP:
//   pico::HTTPServer server(io, 8080, "./www");
//   server.router().get("/hello", handler);
//   io.run();
//
// HTTPS (requires PICO_ENABLE_TLS):
//   auto ctx = pico::ssl::make_server_context("cert.pem", "key.pem");
//   pico::HTTPServer server(io, 8443, "./www");
//   server.use_tls(std::move(ctx));
//   io.run();
//
// TODO: HTTP/2 — would require nghttp2 for ALPN negotiation and multiplexed
//   stream handling; a significant architectural addition. Consider a separate
//   H2Server class to avoid breaking the HTTP/1.1 path.
// TODO: HTTP/3 — would require ngtcp2 + QUIC transport support in ASIO.
// ---------------------------------------------------------------------------

#include "request.hpp"
#include "response.hpp"
#include "router.hpp"
#include "static_files.hpp"
#include "websocket.hpp"
#include <asio.hpp>
#include <functional>
#include <memory>
#include <string>

#ifdef PICO_ENABLE_TLS
#include "ssl_context.hpp"
#include <asio/ssl.hpp>
#endif

using asio::ip::tcp;

namespace pico {

// ---------------------------------------------------------------------------
// HTTPServerConnection<Socket>  — per-connection state machine
//
// Lifecycle:
//   start()
//     └─ readRequest()
//           └─ handleRequest()            (parse + dispatch through Router)
//                 └─ sendResponse()
//                       ├─ if keep-alive → readRequest()  (loop)
//                       └─ if Connection: Upgrade (WS) → upgrade()
// ---------------------------------------------------------------------------
template<typename Socket>
class HTTPServerConnection
    : public std::enable_shared_from_this<HTTPServerConnection<Socket>> {
public:
    HTTPServerConnection(Socket socket,
                         const Router& router,
                         const std::string& root_dir,
                         unsigned max_keepalive,
                         const StaticConfig& static_cfg)
        : socket_(std::move(socket))
        , router_(router)
        , root_dir_(root_dir)
        , max_keepalive_(max_keepalive)
        , static_cfg_(static_cfg) {}

    void start() { readRequest(); }

private:
    void do_shutdown() {
        std::error_code ec;
        if constexpr (std::is_same_v<Socket, tcp::socket>)
            socket_.shutdown(tcp::socket::shutdown_both, ec);
        else
            socket_.lowest_layer().shutdown(tcp::socket::shutdown_both, ec);
    }

    void readRequest() {
        if (requests_served_ >= max_keepalive_) {
            do_shutdown();
            return;
        }
        auto self = this->shared_from_this();
        asio::async_read_until(
            socket_, asio::dynamic_buffer(read_buf_), "\r\n\r\n",
            [this, self](std::error_code ec, size_t header_bytes) {
                if (ec) return;
                handleRequest(header_bytes);
            });
    }

    void handleRequest(size_t header_bytes) {
        std::string header_str = read_buf_.substr(0, header_bytes);

        HTTPRequest parser(header_str);
        if (!parser.parse()) {
            read_buf_.clear();
            sendRaw(Response::bad_request().serialize(), false);
            return;
        }

        read_buf_.erase(0, header_bytes);

        size_t content_len = 0;
        const auto& cl = parser.getHeader("Content-Length");
        if (!cl.empty()) {
            try { content_len = static_cast<size_t>(std::stoul(cl)); }
            catch (...) {}
        }

        // Reject oversized payloads to prevent memory-exhaustion DoS
        constexpr size_t max_body_size = 10 * 1024 * 1024; // 10 MB
        if (content_len > max_body_size) {
            read_buf_.clear();
            sendRaw(Response::make(StatusCode::PayloadTooLarge).serialize(), false);
            return;
        }

        if (content_len > 0 && read_buf_.size() < content_len) {
            size_t remaining = content_len - read_buf_.size();
            auto self = this->shared_from_this();
            asio::async_read(
                socket_, asio::dynamic_buffer(read_buf_),
                asio::transfer_exactly(remaining),
                [this, self, header_str, content_len](std::error_code ec, size_t) {
                    if (ec) return;
                    dispatchRequest(header_str, read_buf_.substr(0, content_len));
                    read_buf_.erase(0, content_len);
                });
        } else {
            std::string body = read_buf_.substr(0, content_len);
            read_buf_.erase(0, content_len);
            dispatchRequest(header_str, std::move(body));
        }
    }

    void dispatchRequest(const std::string& header_str, std::string body) {
        auto opt = parse_request(header_str, std::move(body));
        if (!opt) {
            sendRaw(Response::bad_request().serialize(), false);
            return;
        }
        Request& req = *opt;
        bool keep = req.keepAlive();

        // WebSocket upgrade?
        auto upgrade_hdr = req.header("Upgrade");
        if (upgrade_hdr == "websocket" || upgrade_hdr == "Websocket") {
            handleWebSocketUpgrade(req);
            return;
        }

        Response res = Response::make();

        if (!router_.dispatch(req, res)) {
            // No route matched — try static file serving, else 404
            if (req.method == Method::GET) {
                res = serve_static(req, root_dir_, static_cfg_);
            } else {
                res = Response::not_found();
            }
        }

        res.header("Connection", keep ? "keep-alive" : "close");
        ++requests_served_;
        sendRaw(res.serialize(), keep);
    }

    void handleWebSocketUpgrade(Request& req) {
        auto key = req.header("Sec-WebSocket-Key");
        if (key.empty()) {
            sendRaw(Response::bad_request("Missing Sec-WebSocket-Key").serialize(), false);
            return;
        }
        auto accept = ws::ws_accept_key(key);
        auto upgrade_response = Response::switching_protocols(accept);
        auto wire = std::make_shared<std::string>(upgrade_response.serialize());
        auto self = this->shared_from_this();
        asio::async_write(socket_, asio::buffer(*wire),
            [this, self, wire](std::error_code ec, size_t) {
                if (ec) return;
                // Hand off socket to WebSocketConnection.
                // For TLS streams, extract the underlying tcp::socket.
                if constexpr (std::is_same_v<Socket, tcp::socket>) {
                    auto ws_conn = std::make_shared<ws::WebSocketConnection>(std::move(socket_));
                    if (ws_handler_) ws_handler_(ws_conn);
                    ws_conn->start();
                } else {
                    auto ws_conn = std::make_shared<ws::WebSocketConnection>(
                        std::move(socket_.next_layer()));
                    if (ws_handler_) ws_handler_(ws_conn);
                    ws_conn->start();
                }
            });
    }

    void sendRaw(std::string data, bool keep_alive) {
        auto wire = std::make_shared<std::string>(std::move(data));
        auto self = this->shared_from_this();
        asio::async_write(socket_, asio::buffer(*wire),
            [this, self, wire, keep_alive](std::error_code ec, size_t) {
                if (!ec && keep_alive) {
                    readRequest();
                } else if (!ec) {
                    do_shutdown();
                }
            });
    }

    Socket        socket_;
    const Router& router_;
    std::string   root_dir_;
    std::string   read_buf_;
    unsigned      max_keepalive_;
    unsigned      requests_served_ = 0;
    StaticConfig  static_cfg_;

    std::function<void(std::shared_ptr<ws::WebSocketConnection>)> ws_handler_;

    template<typename S> friend class HTTPServerImpl;
    friend class HTTPServer;
};

// ---------------------------------------------------------------------------
// HTTPServer
// ---------------------------------------------------------------------------
class HTTPServer {
public:
    HTTPServer(asio::io_context& io, unsigned short port,
               std::string root_dir = "./www")
        : acceptor_(io, tcp::endpoint(tcp::v4(), port))
        , root_dir_(std::move(root_dir)) {
        startAccept();
    }

    /// Access the router to register routes + middleware before running.
    Router& router() { return router_; }

    /// Access/modify the static file serving configuration.
    StaticConfig& static_config() { return static_cfg_; }

    /// Register a WebSocket upgrade handler.
    HTTPServer& on_websocket(
            std::function<void(std::shared_ptr<ws::WebSocketConnection>)> h) {
        ws_handler_ = std::move(h);
        return *this;
    }

    /// Set maximum requests per keep-alive connection (default 100).
    HTTPServer& max_keepalive(unsigned n) { max_keepalive_ = n; return *this; }

#ifdef PICO_ENABLE_TLS
    /// Enable TLS — call before io.run().
    /// Takes a fully configured asio::ssl::context (use ssl_context.hpp helpers).
    HTTPServer& use_tls(asio::ssl::context ctx) {
        ssl_ctx_ = std::make_shared<asio::ssl::context>(std::move(ctx));
        return *this;
    }

    bool tls_enabled() const { return ssl_ctx_ != nullptr; }
#endif

private:
    void startAccept() {
        acceptor_.async_accept(
            [this](std::error_code ec, tcp::socket socket) {
                if (!ec) {
#ifdef PICO_ENABLE_TLS
                    if (ssl_ctx_) {
                        // Wrap in SSL stream and perform handshake before session
                        auto ssl_sock = std::make_shared<asio::ssl::stream<tcp::socket>>(
                            std::move(socket), *ssl_ctx_);
                        ssl_sock->async_handshake(
                            asio::ssl::stream_base::server,
                            [this, ssl_sock](std::error_code hec) {
                                if (!hec) {
                                    auto conn = std::make_shared<
                                        HTTPServerConnection<asio::ssl::stream<tcp::socket>>>(
                                        std::move(*ssl_sock), router_, root_dir_,
                                        max_keepalive_, static_cfg_);
                                    conn->ws_handler_ = ws_handler_;
                                    conn->start();
                                }
                                startAccept();
                            });
                        return; // startAccept() called inside handshake callback
                    }
#endif
                    auto conn = std::make_shared<HTTPServerConnection<tcp::socket>>(
                        std::move(socket), router_, root_dir_,
                        max_keepalive_, static_cfg_);
                    conn->ws_handler_ = ws_handler_;
                    conn->start();
                }
                startAccept();
            });
    }

    tcp::acceptor acceptor_;
    Router        router_;
    std::string   root_dir_;
    unsigned      max_keepalive_ = 100;
    StaticConfig  static_cfg_;

    std::function<void(std::shared_ptr<ws::WebSocketConnection>)> ws_handler_;

#ifdef PICO_ENABLE_TLS
    std::shared_ptr<asio::ssl::context> ssl_ctx_;
#endif
};

} // namespace pico
