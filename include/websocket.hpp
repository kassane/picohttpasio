#pragma once

#include "response.hpp"
#include <asio.hpp>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using asio::ip::tcp;

namespace pico {
namespace ws {

// ---------------------------------------------------------------------------
// SHA-1 (RFC 3174) — inline, no external crypto dependency
// Used only for the WebSocket handshake key computation.
// ---------------------------------------------------------------------------
namespace detail {

inline std::array<uint32_t, 5> sha1(const uint8_t* data, size_t len) {
    uint32_t h0 = 0x67452301u;
    uint32_t h1 = 0xEFCDAB89u;
    uint32_t h2 = 0x98BADCFEu;
    uint32_t h3 = 0x10325476u;
    uint32_t h4 = 0xC3D2E1F0u;

    // Pre-processing: padding the message
    std::vector<uint8_t> msg(data, data + len);
    msg.push_back(0x80);
    while ((msg.size() % 64) != 56) msg.push_back(0x00);
    uint64_t bit_len = static_cast<uint64_t>(len) * 8;
    for (int i = 7; i >= 0; --i)
        msg.push_back(static_cast<uint8_t>((bit_len >> (i * 8)) & 0xFF));

    auto rotl = [](uint32_t v, unsigned n) -> uint32_t {
        return (v << n) | (v >> (32 - n));
    };

    for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(msg[chunk + i*4])     << 24)
                 | (static_cast<uint32_t>(msg[chunk + i*4 + 1]) << 16)
                 | (static_cast<uint32_t>(msg[chunk + i*4 + 2]) <<  8)
                 |  static_cast<uint32_t>(msg[chunk + i*4 + 3]);
        }
        for (int i = 16; i < 80; ++i)
            w[i] = rotl(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);

        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if      (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d;           k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
            else             { f = b ^ c ^ d;           k = 0xCA62C1D6u; }
            uint32_t tmp = rotl(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rotl(b, 30); b = a; a = tmp;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }
    return {h0, h1, h2, h3, h4};
}

inline std::string base64_encode(const uint8_t* data, size_t len) {
    static constexpr char tab[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((len + 2) / 3 * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len) v |= static_cast<uint32_t>(data[i+1]) << 8;
        if (i + 2 < len) v |= static_cast<uint32_t>(data[i+2]);
        out += tab[(v >> 18) & 0x3F];
        out += tab[(v >> 12) & 0x3F];
        out += (i + 1 < len) ? tab[(v >>  6) & 0x3F] : '=';
        out += (i + 2 < len) ? tab[(v >>  0) & 0x3F] : '=';
    }
    return out;
}

} // namespace detail

// ---------------------------------------------------------------------------
// Compute Sec-WebSocket-Accept from the client's Sec-WebSocket-Key
// ---------------------------------------------------------------------------
inline std::string ws_accept_key(std::string_view client_key) {
    static constexpr std::string_view magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string input(client_key);
    input += magic;
    auto hash = detail::sha1(reinterpret_cast<const uint8_t*>(input.data()), input.size());
    uint8_t raw[20];
    for (int i = 0; i < 5; ++i) {
        raw[i*4]   = (hash[i] >> 24) & 0xFF;
        raw[i*4+1] = (hash[i] >> 16) & 0xFF;
        raw[i*4+2] = (hash[i] >>  8) & 0xFF;
        raw[i*4+3] =  hash[i]        & 0xFF;
    }
    return detail::base64_encode(raw, 20);
}

// ---------------------------------------------------------------------------
// WebSocket frame codec
// ---------------------------------------------------------------------------

enum class Opcode : uint8_t {
    Continuation = 0x0,
    Text         = 0x1,
    Binary       = 0x2,
    Close        = 0x8,
    Ping         = 0x9,
    Pong         = 0xA,
};

struct Frame {
    bool    fin     = true;
    Opcode  opcode  = Opcode::Text;
    bool    masked  = false;
    std::array<uint8_t,4> mask_key{};
    std::string payload;
};

// Encode a frame for sending from server (unmasked, server→client)
inline std::string encode(const Frame& f) {
    std::string out;
    uint8_t byte1 = (f.fin ? 0x80 : 0x00) | static_cast<uint8_t>(f.opcode);
    out += static_cast<char>(byte1);
    size_t plen = f.payload.size();
    if (plen <= 125) {
        out += static_cast<char>(plen);
    } else if (plen <= 0xFFFF) {
        out += static_cast<char>(126);
        out += static_cast<char>((plen >> 8) & 0xFF);
        out += static_cast<char>( plen       & 0xFF);
    } else {
        out += static_cast<char>(127);
        for (int s = 56; s >= 0; s -= 8)
            out += static_cast<char>((plen >> s) & 0xFF);
    }
    out += f.payload;
    return out;
}

// Decode one frame from buffer.
// Returns bytes consumed (>0) on success, 0 if more data needed, SIZE_MAX on error.
inline size_t decode(std::string_view buf, Frame& frame_out) {
    if (buf.size() < 2) return 0;

    uint8_t b0 = static_cast<uint8_t>(buf[0]);
    uint8_t b1 = static_cast<uint8_t>(buf[1]);

    frame_out.fin    = (b0 & 0x80) != 0;
    frame_out.opcode = static_cast<Opcode>(b0 & 0x0F);
    frame_out.masked = (b1 & 0x80) != 0;

    size_t payload_len = b1 & 0x7F;
    size_t offset = 2;

    if (payload_len == 126) {
        if (buf.size() < 4) return 0;
        payload_len = (static_cast<size_t>(static_cast<uint8_t>(buf[2])) << 8)
                    |  static_cast<size_t>(static_cast<uint8_t>(buf[3]));
        offset = 4;
    } else if (payload_len == 127) {
        if (buf.size() < 10) return 0;
        payload_len = 0;
        for (int i = 2; i < 10; ++i)
            payload_len = (payload_len << 8) | static_cast<uint8_t>(buf[i]);
        offset = 10;
    }

    if (frame_out.masked) {
        if (buf.size() < offset + 4) return 0;
        frame_out.mask_key = {
            static_cast<uint8_t>(buf[offset]),
            static_cast<uint8_t>(buf[offset+1]),
            static_cast<uint8_t>(buf[offset+2]),
            static_cast<uint8_t>(buf[offset+3])
        };
        offset += 4;
    }

    if (buf.size() < offset + payload_len) return 0;

    frame_out.payload.assign(buf.data() + offset, payload_len);

    if (frame_out.masked) {
        for (size_t i = 0; i < frame_out.payload.size(); ++i)
            frame_out.payload[i] ^= static_cast<char>(frame_out.mask_key[i % 4]);
    }

    return offset + payload_len;
}

// ---------------------------------------------------------------------------
// WebSocketConnection — async read/write loop over an already-upgraded socket
// ---------------------------------------------------------------------------
class WebSocketConnection
    : public std::enable_shared_from_this<WebSocketConnection> {
public:
    using MessageHandler = std::function<void(Frame)>;
    using CloseHandler   = std::function<void()>;

    explicit WebSocketConnection(tcp::socket socket)
        : socket_(std::move(socket)) {}

    void on_message(MessageHandler h)  { on_message_ = std::move(h); }
    void on_close(CloseHandler h)      { on_close_   = std::move(h); }

    void send(std::string data, Opcode opcode = Opcode::Text) {
        Frame f;
        f.opcode  = opcode;
        f.payload = std::move(data);
        auto wire = std::make_shared<std::string>(encode(f));
        asio::async_write(socket_, asio::buffer(*wire),
            [wire, self = shared_from_this()](std::error_code, size_t) {});
    }

    void close() {
        send({}, Opcode::Close);
        std::error_code ec;
        socket_.shutdown(tcp::socket::shutdown_both, ec);
    }

    void start() { read_frame(); }

private:
    void read_frame() {
        auto self = shared_from_this();
        asio::async_read_until(
            socket_, asio::dynamic_buffer(read_buf_), "\x00",
            [this, self](std::error_code, size_t) {
                // We don't use read_until's delimiter — just ensure we keep reading
                process_frames();
            });
        // Note: for WebSocket we need a raw read loop, not read_until.
        // Re-implement with async_read_some:
        socket_.async_read_some(
            asio::buffer(raw_buf_),
            [this, self](std::error_code ec, size_t n) {
                if (ec) {
                    if (on_close_) on_close_();
                    return;
                }
                read_buf_.append(raw_buf_.data(), n);
                process_frames();
                read_frame();
            });
    }

    void process_frames() {
        while (true) {
            Frame frame;
            size_t consumed = decode(read_buf_, frame);
            if (consumed == 0) break;          // need more data
            read_buf_.erase(0, consumed);

            switch (frame.opcode) {
                case Opcode::Close:
                    close();
                    if (on_close_) on_close_();
                    return;
                case Opcode::Ping:
                    send(frame.payload, Opcode::Pong);
                    break;
                case Opcode::Pong:
                    break; // ignore
                default:
                    if (on_message_) on_message_(std::move(frame));
            }
        }
    }

    tcp::socket   socket_;
    std::string   read_buf_;
    std::array<char, 4096> raw_buf_{};
    MessageHandler on_message_;
    CloseHandler   on_close_;
};

} // namespace ws
} // namespace pico
