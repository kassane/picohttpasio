#include <catch2/catch_test_macros.hpp>
#include "websocket.hpp"
#include <string>
#include <cstdint>
#include <array>

using namespace pico::ws;

// ---------------------------------------------------------------------------
// ws_accept_key — RFC 6455 test vector
// Key = "dGhlIHNhbXBsZSBub25jZQ==" → Accept = "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="
// ---------------------------------------------------------------------------
TEST_CASE("ws_accept_key produces correct RFC 6455 handshake key", "[websocket]") {
    // This is the example from RFC 6455 §1.3
    auto accept = ws_accept_key("dGhlIHNhbXBsZSBub25jZQ==");
    REQUIRE(accept == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

TEST_CASE("ws_accept_key produces different outputs for different inputs", "[websocket]") {
    auto a1 = ws_accept_key("AAAA");
    auto a2 = ws_accept_key("BBBB");
    REQUIRE(a1 != a2);
}

// ---------------------------------------------------------------------------
// Frame encoding
// ---------------------------------------------------------------------------

TEST_CASE("encode: short text frame (FIN, unmasked)", "[websocket][codec]") {
    Frame f;
    f.fin     = true;
    f.opcode  = Opcode::Text;
    f.payload = "Hi";

    auto wire = encode(f);
    // byte0: 0x81 (FIN | opcode=Text)
    REQUIRE(static_cast<uint8_t>(wire[0]) == 0x81);
    // byte1: 0x02 (no mask, length=2)
    REQUIRE(static_cast<uint8_t>(wire[1]) == 0x02);
    REQUIRE(wire.substr(2) == "Hi");
}

TEST_CASE("encode: binary frame", "[websocket][codec]") {
    Frame f;
    f.opcode  = Opcode::Binary;
    f.payload = std::string("\x01\x02\x03", 3);
    auto wire = encode(f);
    REQUIRE(static_cast<uint8_t>(wire[0]) == 0x82); // FIN | Binary
    REQUIRE(static_cast<uint8_t>(wire[1]) == 0x03); // length=3
}

TEST_CASE("encode: empty close frame", "[websocket][codec]") {
    Frame f;
    f.opcode  = Opcode::Close;
    f.payload = {};
    auto wire = encode(f);
    REQUIRE(static_cast<uint8_t>(wire[0]) == 0x88); // FIN | Close
    REQUIRE(static_cast<uint8_t>(wire[1]) == 0x00); // length=0
}

TEST_CASE("encode: 126-byte extended payload length header", "[websocket][codec]") {
    Frame f;
    f.opcode  = Opcode::Text;
    f.payload = std::string(200, 'x');
    auto wire = encode(f);
    REQUIRE(static_cast<uint8_t>(wire[1]) == 126);
    // next 2 bytes = 200
    REQUIRE(static_cast<uint8_t>(wire[2]) == 0);
    REQUIRE(static_cast<uint8_t>(wire[3]) == 200);
    REQUIRE(wire.size() == 4 + 200);
}

// ---------------------------------------------------------------------------
// Frame decoding
// ---------------------------------------------------------------------------

TEST_CASE("decode: round-trips short text frame", "[websocket][codec]") {
    Frame src;
    src.opcode  = Opcode::Text;
    src.payload = "hello";
    auto wire   = encode(src);

    Frame dst;
    size_t consumed = decode(wire, dst);
    REQUIRE(consumed == wire.size());
    REQUIRE(dst.fin);
    REQUIRE(dst.opcode  == Opcode::Text);
    REQUIRE(dst.payload == "hello");
}

TEST_CASE("decode: returns 0 if data incomplete", "[websocket][codec]") {
    Frame f;
    f.opcode  = Opcode::Text;
    f.payload = "hello";
    auto wire = encode(f);

    // Provide only 1 byte
    Frame out;
    REQUIRE(decode(std::string_view(wire).substr(0, 1), out) == 0);
}

TEST_CASE("decode: masked client frame is unmasked correctly", "[websocket][codec]") {
    // Build a manually masked frame: "Hello" with mask 0x37FA213D
    // From RFC 6455 §5.7 example
    std::string wire = {
        '\x81',             // FIN + Text
        '\x85',             // MASK set + length 5
        '\x37', '\xfa', '\x21', '\x3d',  // masking key
        char(0x48 ^ 0x37),  // H
        char(0x65 ^ 0xfa),  // e
        char(0x6c ^ 0x21),  // l
        char(0x6c ^ 0x3d),  // l
        char(0x6f ^ 0x37),  // o
    };

    Frame f;
    size_t consumed = decode(wire, f);
    REQUIRE(consumed == wire.size());
    REQUIRE(f.masked);
    REQUIRE(f.payload == "Hello");
}

TEST_CASE("decode: ping frame", "[websocket][codec]") {
    Frame src;
    src.opcode  = Opcode::Ping;
    src.payload = "ping-data";
    auto wire   = encode(src);

    Frame dst;
    decode(wire, dst);
    REQUIRE(dst.opcode  == Opcode::Ping);
    REQUIRE(dst.payload == "ping-data");
}

TEST_CASE("decode: handles multiple frames in sequence", "[websocket][codec]") {
    Frame f1, f2;
    f1.payload = "first";
    f2.payload = "second";
    std::string wire = encode(f1) + encode(f2);

    Frame out;
    size_t c1 = decode(wire, out);
    REQUIRE(c1 > 0);
    REQUIRE(out.payload == "first");

    size_t c2 = decode(std::string_view(wire).substr(c1), out);
    REQUIRE(c2 > 0);
    REQUIRE(out.payload == "second");
}

TEST_CASE("decode: 126-byte payload length extension round-trip", "[websocket][codec]") {
    Frame src;
    src.payload = std::string(200, 'A');
    auto wire   = encode(src);

    Frame dst;
    size_t consumed = decode(wire, dst);
    REQUIRE(consumed == wire.size());
    REQUIRE(dst.payload == src.payload);
}

// ---------------------------------------------------------------------------
// detail::base64_encode
// ---------------------------------------------------------------------------
TEST_CASE("base64_encode known vectors", "[websocket][detail]") {
    using pico::ws::detail::base64_encode;
    // RFC 4648 test vectors
    {
        std::string in = "";
        REQUIRE(base64_encode(reinterpret_cast<const uint8_t*>(in.data()), in.size()) == "");
    }
    {
        std::string in = "f";
        REQUIRE(base64_encode(reinterpret_cast<const uint8_t*>(in.data()), in.size()) == "Zg==");
    }
    {
        std::string in = "fo";
        REQUIRE(base64_encode(reinterpret_cast<const uint8_t*>(in.data()), in.size()) == "Zm8=");
    }
    {
        std::string in = "foo";
        REQUIRE(base64_encode(reinterpret_cast<const uint8_t*>(in.data()), in.size()) == "Zm9v");
    }
}
