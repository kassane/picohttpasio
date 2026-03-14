// websocket_client.cpp — async WebSocket client example
//
// Connects to a WebSocket echo server, exchanges frames, and disconnects cleanly.
// Works with the bundled websocket_echo server or any RFC-6455-compliant server.
//
// Build:
//   cmake -B build && cmake --build build
//   # Terminal 1 — start the echo server:
//   ./build/examples/websocket_echo
//   # Terminal 2 — run this client:
//   ./build/examples/websocket_client [host] [port] [path]
//   ./build/examples/websocket_client localhost 8080 /ws

#include <websocket.hpp>
#include <asio.hpp>
#include <array>
#include <functional>
#include <iostream>
#include <memory>
#include <random>
#include <string>

using asio::ip::tcp;

// ---------------------------------------------------------------------------
// Masked client→server frame encoder (RFC 6455 §5.3 — clients MUST mask)
// ---------------------------------------------------------------------------
static std::string encode_client_frame(const pico::ws::Frame& f,
                                       std::array<uint8_t, 4> mask_key) {
    std::string out;
    out += static_cast<char>((f.fin ? 0x80u : 0x00u)
                              | static_cast<uint8_t>(f.opcode));

    const size_t plen = f.payload.size();
    if (plen <= 125) {
        out += static_cast<char>(0x80u | plen);   // MASK bit set
    } else if (plen <= 0xFFFF) {
        out += static_cast<char>(0x80u | 126u);
        out += static_cast<char>((plen >> 8) & 0xFF);
        out += static_cast<char>( plen       & 0xFF);
    } else {
        out += static_cast<char>(0x80u | 127u);
        for (int s = 56; s >= 0; s -= 8)
            out += static_cast<char>((plen >> s) & 0xFF);
    }

    // 4-byte masking key
    for (auto b : mask_key) out += static_cast<char>(b);

    // Masked payload
    for (size_t i = 0; i < plen; ++i)
        out += static_cast<char>(f.payload[i] ^ static_cast<char>(mask_key[i % 4]));

    return out;
}

// ---------------------------------------------------------------------------
// WebSocketClient — async client supporting the full RFC 6455 lifecycle
// ---------------------------------------------------------------------------
class WebSocketClient : public std::enable_shared_from_this<WebSocketClient> {
public:
    using MessageHandler = std::function<void(pico::ws::Frame)>;
    using CloseHandler   = std::function<void()>;
    using OpenHandler    = std::function<void()>;

    WebSocketClient(asio::io_context& io, std::string host, unsigned short port)
        : socket_(io), resolver_(io)
        , host_(std::move(host)), port_(port)
        , rng_(std::random_device{}()) {}

    void on_open   (OpenHandler    h) { on_open_    = std::move(h); }
    void on_message(MessageHandler h) { on_message_ = std::move(h); }
    void on_close  (CloseHandler   h) { on_close_   = std::move(h); }

    // Connect and perform the WS upgrade handshake.
    void connect(std::string path = "/ws") {
        path_ = std::move(path);
        resolver_.async_resolve(host_, std::to_string(port_),
            [self = shared_from_this()](std::error_code ec, tcp::resolver::results_type eps) {
                if (ec) { std::cerr << "Resolve: " << ec.message() << "\n"; return; }
                asio::async_connect(self->socket_, eps,
                    [self](std::error_code ec, tcp::endpoint) {
                        if (ec) { std::cerr << "Connect: " << ec.message() << "\n"; return; }
                        self->do_handshake();
                    });
            });
    }

    // Send a text message.
    void send(std::string text, pico::ws::Opcode opcode = pico::ws::Opcode::Text) {
        pico::ws::Frame f;
        f.opcode  = opcode;
        f.payload = std::move(text);

        // Generate a fresh random masking key per frame (RFC 6455 §5.3)
        std::array<uint8_t, 4> mask;
        std::uniform_int_distribution<unsigned> dist(0, 255);
        for (auto& b : mask) b = static_cast<uint8_t>(dist(rng_));

        auto wire = std::make_shared<std::string>(encode_client_frame(f, mask));
        asio::async_write(socket_, asio::buffer(*wire),
            [wire, self = shared_from_this()](std::error_code ec, size_t) {
                if (ec) std::cerr << "Send: " << ec.message() << "\n";
            });
    }

    // Send a close frame and shut down.
    void close(std::string reason = {}) {
        send(std::move(reason), pico::ws::Opcode::Close);
        std::error_code ec;
        socket_.shutdown(tcp::socket::shutdown_both, ec);
    }

private:
    // Build a random 16-byte WS key (base64-encoded).
    std::string make_ws_key() {
        uint8_t raw[16];
        std::uniform_int_distribution<unsigned> d(0, 255);
        for (auto& b : raw) b = static_cast<uint8_t>(d(rng_));
        return pico::ws::detail::base64_encode(raw, 16);
    }

    void do_handshake() {
        ws_key_ = make_ws_key();

        std::string req =
            "GET " + path_ + " HTTP/1.1\r\n"
            "Host: " + host_ + "\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: " + ws_key_ + "\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n";

        auto wire = std::make_shared<std::string>(std::move(req));
        asio::async_write(socket_, asio::buffer(*wire),
            [wire, self = shared_from_this()](std::error_code ec, size_t) {
                if (ec) { std::cerr << "Handshake write: " << ec.message() << "\n"; return; }
                self->read_handshake_response();
            });
    }

    void read_handshake_response() {
        asio::async_read_until(socket_, asio::dynamic_buffer(read_buf_), "\r\n\r\n",
            [self = shared_from_this()](std::error_code ec, size_t n) {
                if (ec) { std::cerr << "Handshake read: " << ec.message() << "\n"; return; }

                // Must be 101 Switching Protocols
                if (self->read_buf_.find("101") == std::string::npos) {
                    std::cerr << "WebSocket upgrade rejected:\n" << self->read_buf_ << "\n";
                    return;
                }

                // Validate Sec-WebSocket-Accept (RFC 6455 §4.1)
                std::string expected = pico::ws::ws_accept_key(self->ws_key_);
                if (self->read_buf_.find(expected) == std::string::npos) {
                    std::cerr << "Bad Sec-WebSocket-Accept (expected " << expected << ")\n";
                    return;
                }

                self->read_buf_.erase(0, n);   // discard HTTP headers

                if (self->on_open_) self->on_open_();
                self->read_frames();
            });
    }

    void read_frames() {
        socket_.async_read_some(asio::buffer(raw_buf_),
            [self = shared_from_this()](std::error_code ec, size_t n) {
                if (ec) {
                    if (self->on_close_) self->on_close_();
                    return;
                }
                self->read_buf_.append(self->raw_buf_.data(), n);
                self->process_frames();
                self->read_frames();   // continue reading
            });
    }

    void process_frames() {
        while (true) {
            pico::ws::Frame frame;
            size_t consumed = pico::ws::decode(read_buf_, frame);
            if (consumed == 0) break;
            read_buf_.erase(0, consumed);

            switch (frame.opcode) {
                case pico::ws::Opcode::Close:
                    close();
                    if (on_close_) on_close_();
                    return;
                case pico::ws::Opcode::Ping: {
                    // Respond with a masked Pong
                    pico::ws::Frame pong;
                    pong.opcode  = pico::ws::Opcode::Pong;
                    pong.payload = frame.payload;
                    std::array<uint8_t, 4> mask{};
                    auto wire = std::make_shared<std::string>(encode_client_frame(pong, mask));
                    asio::async_write(socket_, asio::buffer(*wire),
                        [wire, self = shared_from_this()](std::error_code, size_t) {});
                    break;
                }
                case pico::ws::Opcode::Pong:
                    break;   // ignore unsolicited pongs
                default:
                    if (on_message_) on_message_(std::move(frame));
            }
        }
    }

    tcp::socket    socket_;
    tcp::resolver  resolver_;
    std::string    host_;
    unsigned short port_;
    std::string    path_;
    std::string    ws_key_;

    std::string            read_buf_;
    std::array<char, 4096> raw_buf_{};

    OpenHandler    on_open_;
    MessageHandler on_message_;
    CloseHandler   on_close_;

    std::mt19937 rng_;
};

// ---------------------------------------------------------------------------
// main — connect to echo server, send three messages, then disconnect
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    const std::string host = (argc >= 2) ? argv[1] : "localhost";
    const unsigned short port =
        (argc >= 3) ? static_cast<unsigned short>(std::stoi(argv[2])) : 8080;
    const std::string path = (argc >= 4) ? argv[3] : "/ws";

    try {
        asio::io_context io;

        auto client = std::make_shared<WebSocketClient>(io, host, port);

        client->on_open([&client]() {
            std::cout << "WebSocket opened\n";

            // Send a few messages immediately after the upgrade
            client->send("Hello, server!");
            client->send("ping #2");
            client->send("ping #3");
        });

        client->on_message([&client](pico::ws::Frame frame) {
            const char* type = (frame.opcode == pico::ws::Opcode::Binary)
                                ? "binary" : "text";
            std::cout << "[" << type << "] " << frame.payload << "\n";

            // Close after the 3rd echo is received (simple demo)
            static int count = 0;
            if (++count >= 3) {
                std::cout << "All echoes received — closing.\n";
                client->close();
            }
        });

        client->on_close([]() {
            std::cout << "WebSocket closed\n";
        });

        std::cout << "Connecting to ws://" << host << ':' << port << path << " …\n";
        client->connect(path);

        io.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
