#pragma once

// ---------------------------------------------------------------------------
// coro_server.hpp — C++20 stackless-coroutine HTTP (and HTTPS) server
//
// Uses asio::awaitable<T> / co_await / asio::co_spawn for clean,
// readable async code — no callbacks, no shared_ptr connection state.
//
// Plain HTTP:
//   CoroServer server(io, 8080, "./www");
//   server.router().get("/hello", handler);
//   server.run();   // spawns accept coroutine
//   io.run();
//
// HTTPS (requires PICO_ENABLE_TLS):
//   auto ctx = pico::ssl::make_server_context("cert.pem", "key.pem");
//   server.use_tls(std::move(ctx));
//   server.run(); io.run();
//
// TODO: HTTP/2 — would require nghttp2 (https://nghttp2.org/) for ALPN
//   negotiation and multiplexed stream handling. Significant architectural
//   change required. Consider a separate H2Server class to avoid breaking
//   the HTTP/1.1 path.
// TODO: HTTP/3 — would require ngtcp2 (https://nghttp2.org/ngtcp2/) and
//   QUIC transport support. Not feasible without a dedicated QUIC socket
//   abstraction in ASIO.
// ---------------------------------------------------------------------------

#include "picohttpwrapper.hpp"
#include "request.hpp"
#include "response.hpp"
#include "router.hpp"
#include "static_files.hpp"
#include "websocket.hpp"

#include <asio.hpp>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/use_awaitable.hpp>

#ifdef PICO_ENABLE_TLS
#include "ssl_context.hpp"
#include <asio/ssl.hpp>
#include <memory>
#endif

#include <functional>
#include <string>
#include <string_view>

using asio::ip::tcp;

namespace pico {

namespace detail {

// Generic session coroutine — works with tcp::socket and ssl::stream<tcp::socket>
template<typename Socket>
asio::awaitable<void> run_session(Socket socket,
                                   const Router& router,
                                   const std::string& root_dir,
                                   unsigned max_keepalive,
                                   const StaticConfig& static_cfg,
                                   std::function<void(std::shared_ptr<ws::WebSocketConnection>)> ws_handler)
{
    std::string buf;
    unsigned served = 0;

    while (served < max_keepalive) {
        // ── Read headers ──────────────────────────────────────────────────
        size_t header_bytes = 0;
        try {
            header_bytes = co_await asio::async_read_until(
                socket, asio::dynamic_buffer(buf), "\r\n\r\n",
                asio::use_awaitable);
        } catch (...) {
            break;  // connection closed or reset
        }

        std::string header_str = buf.substr(0, header_bytes);
        buf.erase(0, header_bytes);

        // ── Parse headers ─────────────────────────────────────────────────
        HTTPRequest parser(header_str);
        if (!parser.parse()) {
            try {
                auto wire = Response::bad_request().serialize();
                co_await asio::async_write(socket, asio::buffer(wire), asio::use_awaitable);
            } catch (...) {}
            break;
        }

        // ── Read body ─────────────────────────────────────────────────────
        size_t content_len = 0;
        const auto& cl_hdr = parser.getHeader("Content-Length");
        if (!cl_hdr.empty()) {
            try { content_len = static_cast<size_t>(std::stoul(cl_hdr)); }
            catch (...) {}
        }

        if (content_len > 0 && buf.size() < content_len) {
            try {
                co_await asio::async_read(
                    socket, asio::dynamic_buffer(buf),
                    asio::transfer_exactly(content_len - buf.size()),
                    asio::use_awaitable);
            } catch (...) { break; }
        }

        std::string body = buf.substr(0, content_len);
        buf.erase(0, content_len);

        // ── Build rich Request ────────────────────────────────────────────
        auto opt = parse_request(header_str, std::move(body));
        if (!opt) {
            try {
                auto wire = Response::bad_request().serialize();
                co_await asio::async_write(socket, asio::buffer(wire), asio::use_awaitable);
            } catch (...) {}
            break;
        }

        Request& req = *opt;
        bool keep_alive = req.keepAlive();

        // ── WebSocket upgrade ─────────────────────────────────────────────
        auto upgrade_hdr = req.header("Upgrade");
        if (upgrade_hdr == "websocket" || upgrade_hdr == "Websocket") {
            auto key = req.header("Sec-WebSocket-Key");
            if (key.empty()) {
                try {
                    auto wire = Response::bad_request("Missing Sec-WebSocket-Key").serialize();
                    co_await asio::async_write(socket, asio::buffer(wire), asio::use_awaitable);
                } catch (...) {}
                co_return;
            }
            auto accept = ws::ws_accept_key(key);
            auto upgrade_resp = Response::switching_protocols(accept).serialize();
            try {
                co_await asio::async_write(socket, asio::buffer(upgrade_resp), asio::use_awaitable);
            } catch (...) { co_return; }

            // Hand off to WebSocketConnection (takes ownership of the raw TCP socket).
            // For SSL streams we extract the underlying tcp::socket via next_layer().
            // Note: after upgrade the SSL state is effectively abandoned — WS frames
            // run over the raw TCP layer.  For true WSS you would wrap WS over the
            // ssl::stream as well, but this covers the common plain-HTTP case.
            if (ws_handler) {
                if constexpr (std::is_same_v<Socket, tcp::socket>) {
                    auto ws_conn = std::make_shared<ws::WebSocketConnection>(
                        std::move(socket));
                    ws_handler(ws_conn);
                    ws_conn->start();
                } else {
                    // SSL stream: extract the underlying tcp::socket
                    auto ws_conn = std::make_shared<ws::WebSocketConnection>(
                        std::move(socket.next_layer()));
                    ws_handler(ws_conn);
                    ws_conn->start();
                }
            }
            co_return;
        }

        // ── Router dispatch ───────────────────────────────────────────────
        Response res = Response::make();
        if (!router.dispatch(req, res)) {
            if (req.method == Method::GET)
                res = pico::serve_static(req, root_dir, static_cfg);
            else
                res = Response::not_found();
        }

        res.header("Connection", keep_alive ? "keep-alive" : "close");
        ++served;

        auto wire = res.serialize();
        try {
            co_await asio::async_write(socket, asio::buffer(wire), asio::use_awaitable);
        } catch (...) { break; }

        if (!keep_alive) break;
    }

    // Graceful TCP-level close
    std::error_code ec;
    if constexpr (std::is_same_v<Socket, tcp::socket>)
        socket.shutdown(tcp::socket::shutdown_both, ec);
    else
        socket.lowest_layer().shutdown(tcp::socket::shutdown_both, ec);
}

} // namespace detail

// ---------------------------------------------------------------------------
// CoroServer
// ---------------------------------------------------------------------------
class CoroServer {
public:
    CoroServer(asio::io_context& io,
               unsigned short port,
               std::string root_dir = "./www")
        : io_(io)
        , acceptor_(io, tcp::endpoint(tcp::v4(), port))
        , root_dir_(std::move(root_dir)) {}

    Router& router() { return router_; }

    /// Access/modify the static file serving configuration.
    StaticConfig& static_config() { return static_cfg_; }

#ifdef PICO_ENABLE_TLS
    // Enable TLS — call before run().
    // Takes a fully configured asio::ssl::context (use ssl_context.hpp helpers).
    CoroServer& use_tls(asio::ssl::context ctx) {
        ssl_ctx_ = std::make_shared<asio::ssl::context>(std::move(ctx));
        return *this;
    }
    bool tls_enabled() const { return ssl_ctx_ != nullptr; }
#endif

    // Register a WebSocket upgrade handler (called after HTTP→WS upgrade).
    CoroServer& on_websocket(
            std::function<void(std::shared_ptr<ws::WebSocketConnection>)> h) {
        ws_handler_ = std::move(h);
        return *this;
    }

    // Max requests per keep-alive connection (default 100).
    CoroServer& max_keepalive(unsigned n) { max_keepalive_ = n; return *this; }

    // Spawn the accept coroutine. Call io.run() after this.
    void run() {
        asio::co_spawn(io_, accept_loop(), asio::detached);
    }

private:
    asio::awaitable<void> accept_loop() {
        for (;;) {
            try {
                auto socket = co_await acceptor_.async_accept(asio::use_awaitable);
                auto exec   = socket.get_executor();

#ifdef PICO_ENABLE_TLS
                if (ssl_ctx_) {
                    auto ctx_ptr = ssl_ctx_;
                    asio::co_spawn(exec,
                        [sock = std::move(socket), ctx_ptr,
                         &router = router_, &root = root_dir_,
                         max_ka = max_keepalive_,
                         static_cfg = static_cfg_,
                         ws_h = ws_handler_]() mutable -> asio::awaitable<void>
                        {
                            asio::ssl::stream<tcp::socket> ssl_sock(std::move(sock), *ctx_ptr);
                            try {
                                co_await ssl_sock.async_handshake(
                                    asio::ssl::stream_base::server, asio::use_awaitable);
                            } catch (...) { co_return; }
                            co_await detail::run_session(
                                std::move(ssl_sock), router, root,
                                max_ka, static_cfg, ws_h);
                        },
                        asio::detached);
                    continue;
                }
#endif
                asio::co_spawn(exec,
                    detail::run_session(
                        std::move(socket), router_, root_dir_,
                        max_keepalive_, static_cfg_, ws_handler_),
                    asio::detached);

            } catch (...) {
                // acceptor closed — exit loop
                break;
            }
        }
    }

    asio::io_context& io_;
    tcp::acceptor     acceptor_;
    Router            router_;
    std::string       root_dir_;
    unsigned          max_keepalive_ = 100;
    StaticConfig      static_cfg_;

    std::function<void(std::shared_ptr<ws::WebSocketConnection>)> ws_handler_;

#ifdef PICO_ENABLE_TLS
    std::shared_ptr<asio::ssl::context> ssl_ctx_;
#endif
};

} // namespace pico
