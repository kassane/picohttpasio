#pragma once

#include "request.hpp"
#include "response.hpp"
#include "router.hpp"
#include "websocket.hpp"
#include <asio.hpp>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <string>

using asio::ip::tcp;

namespace pico {

// ---------------------------------------------------------------------------
// HTTPServerConnection  — per-connection state machine
//
// Lifecycle:
//   start()
//     └─ readRequest()
//           └─ handleRequest()            (parse + dispatch through Router)
//                 └─ sendResponse()
//                       ├─ if keep-alive → readRequest()  (loop)
//                       └─ if Connection: Upgrade (WS) → upgrade()
// ---------------------------------------------------------------------------
class HTTPServerConnection
    : public std::enable_shared_from_this<HTTPServerConnection> {
public:
    HTTPServerConnection(tcp::socket socket,
                         const Router& router,
                         const std::string& root_dir,
                         unsigned max_keepalive = 100)
        : socket_(std::move(socket))
        , router_(router)
        , root_dir_(root_dir)
        , max_keepalive_(max_keepalive) {}

    void start() { readRequest(); }

private:
    void readRequest() {
        if (requests_served_ >= max_keepalive_) {
            std::error_code ignored; socket_.shutdown(tcp::socket::shutdown_both, ignored);
            return;
        }
        auto self = shared_from_this();
        asio::async_read_until(
            socket_, asio::dynamic_buffer(read_buf_), "\r\n\r\n",
            [this, self](std::error_code ec, size_t header_bytes) {
                if (ec) return;
                handleRequest(header_bytes);
            });
    }

    void handleRequest(size_t header_bytes) {
        std::string header_str = read_buf_.substr(0, header_bytes);

        // Tentatively parse headers to find Content-Length / body
        HTTPRequest parser(header_str);
        if (!parser.parse()) {
            read_buf_.clear();
            sendRaw(Response::bad_request().serialize(), false);
            return;
        }

        // Consume header bytes from buffer; remainder may be start of body
        read_buf_.erase(0, header_bytes);

        size_t content_len = 0;
        const auto& cl = parser.getHeader("Content-Length");
        if (!cl.empty()) {
            try { content_len = static_cast<size_t>(std::stoul(cl)); }
            catch (...) {}
        }

        if (content_len > 0 && read_buf_.size() < content_len) {
            size_t remaining = content_len - read_buf_.size();
            auto self = shared_from_this();
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
                res = serveStatic(req.path);
            } else {
                res = Response::not_found();
            }
        }

        // Inject Connection header to match keep-alive decision
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
        auto self = shared_from_this();
        asio::async_write(socket_, asio::buffer(*wire),
            [this, self, wire](std::error_code ec, size_t) {
                if (ec) return;
                // Hand off socket to WebSocketConnection
                auto ws_conn = std::make_shared<ws::WebSocketConnection>(std::move(socket_));
                if (ws_handler_) ws_handler_(ws_conn);
                ws_conn->start();
            });
    }

    void sendRaw(std::string data, bool keep_alive) {
        auto wire = std::make_shared<std::string>(std::move(data));
        auto self = shared_from_this();
        asio::async_write(socket_, asio::buffer(*wire),
            [this, self, wire, keep_alive](std::error_code ec, size_t) {
                if (!ec && keep_alive) {
                    readRequest();
                } else if (!ec) {
                    std::error_code ignored;
                    socket_.shutdown(tcp::socket::shutdown_both, ignored);
                }
            });
    }

    Response serveStatic(const std::string& path) {
        // Prevent directory traversal
        if (path.find("..") != std::string::npos)
            return Response::bad_request("Invalid path");

        std::string file_path = root_dir_;
        if (path == "/") {
            file_path += "/index.html";
        } else {
            file_path += path;
        }

        std::ifstream file(file_path, std::ios::binary);
        if (!file) return Response::not_found();

        std::string contents((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());

        // Guess Content-Type from extension
        std::string_view ct = "application/octet-stream";
        if (file_path.ends_with(".html") || file_path.ends_with(".htm"))
            ct = "text/html; charset=utf-8";
        else if (file_path.ends_with(".css"))
            ct = "text/css";
        else if (file_path.ends_with(".js"))
            ct = "application/javascript";
        else if (file_path.ends_with(".json"))
            ct = "application/json";
        else if (file_path.ends_with(".png"))
            ct = "image/png";
        else if (file_path.ends_with(".jpg") || file_path.ends_with(".jpeg"))
            ct = "image/jpeg";
        else if (file_path.ends_with(".svg"))
            ct = "image/svg+xml";
        else if (file_path.ends_with(".txt"))
            ct = "text/plain; charset=utf-8";

        return Response::make().body(std::move(contents), ct);
    }

    tcp::socket   socket_;
    const Router& router_;
    std::string   root_dir_;
    std::string   read_buf_;
    unsigned      max_keepalive_;
    unsigned      requests_served_ = 0;

    // Optional WS upgrade handler — set by HTTPServer
    std::function<void(std::shared_ptr<ws::WebSocketConnection>)> ws_handler_;
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

    // Access the router to register routes + middleware before running.
    Router& router() { return router_; }

    // Register a WebSocket upgrade handler.
    // Called when a client successfully upgrades; receives the live WsConnection.
    HTTPServer& on_websocket(
            std::function<void(std::shared_ptr<ws::WebSocketConnection>)> h) {
        ws_handler_ = std::move(h);
        return *this;
    }

    // Set maximum requests per keep-alive connection (default 100).
    HTTPServer& max_keepalive(unsigned n) { max_keepalive_ = n; return *this; }

private:
    void startAccept() {
        acceptor_.async_accept(
            [this](std::error_code ec, tcp::socket socket) {
                if (!ec) {
                    auto conn = std::make_shared<HTTPServerConnection>(
                        std::move(socket), router_, root_dir_, max_keepalive_);
                    conn->ws_handler_ = ws_handler_;
                    conn->start();
                }
                startAccept();
            });
    }

    tcp::acceptor  acceptor_;
    Router         router_;
    std::string    root_dir_;
    unsigned       max_keepalive_ = 100;
    std::function<void(std::shared_ptr<ws::WebSocketConnection>)> ws_handler_;
};

} // namespace pico
