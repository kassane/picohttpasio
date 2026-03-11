#include <catch2/catch_test_macros.hpp>
#include "coro_server.hpp"
#include <asio.hpp>
#include <thread>
#include <chrono>
#include <atomic>
#include <string>

// ---------------------------------------------------------------------------
// Synchronous HTTP helper (same as in test_integration.cpp)
// ---------------------------------------------------------------------------
static std::string sync_http(const std::string& host, unsigned short port,
                              const std::string& request) {
    using tcp = asio::ip::tcp;
    asio::io_context io;
    tcp::socket sock(io);
    tcp::resolver resolver(io);
    asio::connect(sock, resolver.resolve(host, std::to_string(port)));
    asio::write(sock, asio::buffer(request));
    std::string buf;
    std::error_code ec;
    asio::read(sock, asio::dynamic_buffer(buf), ec);
    return buf;
}

// ---------------------------------------------------------------------------
// CoroServer plain HTTP tests (ports 18190–18199)
// ---------------------------------------------------------------------------

TEST_CASE("CoroServer: GET /hello returns 200 OK", "[coro]") {
    asio::io_context io;
    pico::CoroServer server(io, 18190);
    server.router().get("/hello", [](pico::Request&, pico::Response& res) {
        res = pico::Response::ok("Hello from coroutine!");
    });
    server.run();

    std::thread t([&io]() { io.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::string resp = sync_http("127.0.0.1", 18190,
        "GET /hello HTTP/1.0\r\nHost: localhost\r\nConnection: close\r\n\r\n");

    io.stop();
    t.join();

    REQUIRE(resp.find("200 OK")              != std::string::npos);
    REQUIRE(resp.find("Hello from coroutine!") != std::string::npos);
}

TEST_CASE("CoroServer: unmatched route returns 404", "[coro]") {
    asio::io_context io;
    pico::CoroServer server(io, 18191);
    server.run();

    std::thread t([&io]() { io.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::string resp = sync_http("127.0.0.1", 18191,
        "GET /missing HTTP/1.0\r\nHost: localhost\r\nConnection: close\r\n\r\n");

    io.stop();
    t.join();

    REQUIRE(resp.find("404") != std::string::npos);
}

TEST_CASE("CoroServer: path params extracted", "[coro]") {
    asio::io_context io;
    pico::CoroServer server(io, 18192);
    server.router().get("/items/:id", [](pico::Request& req, pico::Response& res) {
        res = pico::Response::ok("item:" + std::string(req.param("id")));
    });
    server.run();

    std::thread t([&io]() { io.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::string resp = sync_http("127.0.0.1", 18192,
        "GET /items/99 HTTP/1.0\r\nHost: localhost\r\nConnection: close\r\n\r\n");

    io.stop();
    t.join();

    REQUIRE(resp.find("200") != std::string::npos);
    REQUIRE(resp.find("item:99") != std::string::npos);
}

TEST_CASE("CoroServer: POST body received", "[coro]") {
    asio::io_context io;
    pico::CoroServer server(io, 18193);
    server.router().post("/echo", [](pico::Request& req, pico::Response& res) {
        res = pico::Response::ok(req.body);
    });
    server.run();

    std::thread t([&io]() { io.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::string body = "coro-body";
    std::string request =
        "POST /echo HTTP/1.0\r\n"
        "Host: localhost\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" + body;

    std::string resp = sync_http("127.0.0.1", 18193, request);

    io.stop();
    t.join();

    REQUIRE(resp.find("200")       != std::string::npos);
    REQUIRE(resp.find("coro-body") != std::string::npos);
}

TEST_CASE("CoroServer: keep-alive serves multiple requests", "[coro]") {
    asio::io_context io;
    std::atomic<int> hits{0};
    pico::CoroServer server(io, 18194);
    server.router().get("/ping", [&hits](pico::Request&, pico::Response& res) {
        ++hits;
        res = pico::Response::ok("pong");
    });
    server.run();

    std::thread t([&io]() { io.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    using tcp = asio::ip::tcp;
    asio::io_context cio;
    tcp::socket sock(cio);
    tcp::resolver rslv(cio);
    asio::connect(sock, rslv.resolve("127.0.0.1", "18194"));

    std::string r1 = "GET /ping HTTP/1.1\r\nHost: localhost\r\nConnection: keep-alive\r\n\r\n";
    std::string r2 = "GET /ping HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    asio::write(sock, asio::buffer(r1 + r2));

    std::string buf;
    std::error_code ec;
    asio::read(sock, asio::dynamic_buffer(buf), ec);

    io.stop();
    t.join();

    REQUIRE(hits >= 2);
    size_t count = 0, pos = 0;
    while ((pos = buf.find("200 OK", pos)) != std::string::npos) { ++count; ++pos; }
    REQUIRE(count >= 2);
}

TEST_CASE("CoroServer: middleware runs before handler", "[coro]") {
    asio::io_context io;
    std::atomic<bool> mw_ran{false};

    pico::CoroServer server(io, 18195);
    server.router().use([&mw_ran](pico::Request&, pico::Response&, std::function<void()> next) {
        mw_ran = true;
        next();
    });
    server.router().get("/x", [](pico::Request&, pico::Response& res) {
        res = pico::Response::ok("ok");
    });
    server.run();

    std::thread t([&io]() { io.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    sync_http("127.0.0.1", 18195,
        "GET /x HTTP/1.0\r\nHost: localhost\r\nConnection: close\r\n\r\n");

    io.stop();
    t.join();

    REQUIRE(mw_ran.load());
}

TEST_CASE("CoroServer: JSON response has correct Content-Type", "[coro]") {
    asio::io_context io;
    pico::CoroServer server(io, 18196);
    server.router().get("/api", [](pico::Request&, pico::Response& res) {
        res = pico::Response::make().json(R"({"ok":true})");
    });
    server.run();

    std::thread t([&io]() { io.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::string resp = sync_http("127.0.0.1", 18196,
        "GET /api HTTP/1.0\r\nHost: localhost\r\nConnection: close\r\n\r\n");

    io.stop();
    t.join();

    REQUIRE(resp.find("200 OK")                    != std::string::npos);
    REQUIRE(resp.find("application/json")           != std::string::npos);
    REQUIRE(resp.find(R"({"ok":true})")             != std::string::npos);
}

// ---------------------------------------------------------------------------
// HTTPS tests (only when TLS is compiled in and certs are present)
// ---------------------------------------------------------------------------

#ifdef PICO_ENABLE_TLS

#include "ssl_context.hpp"
#include <asio/ssl.hpp>

// Synchronous HTTPS helper (skips cert verification for self-signed test cert)
static std::string sync_https(const std::string& host, unsigned short port,
                               const std::string& request) {
    using tcp = asio::ip::tcp;
    asio::io_context io;
    asio::ssl::context ssl_ctx(asio::ssl::context::tls_client);
    ssl_ctx.set_verify_mode(asio::ssl::verify_none);  // self-signed in tests

    asio::ssl::stream<tcp::socket> ssl_sock(io, ssl_ctx);
    tcp::resolver resolver(io);
    asio::connect(ssl_sock.lowest_layer(), resolver.resolve(host, std::to_string(port)));
    ssl_sock.handshake(asio::ssl::stream_base::client);

    asio::write(ssl_sock, asio::buffer(request));
    std::string buf;
    std::error_code ec;
    asio::read(ssl_sock, asio::dynamic_buffer(buf), ec);
    return buf;
}

TEST_CASE("CoroServer: HTTPS GET /hello returns 200 OK", "[coro][https]") {
    // Use the test certs generated at build time
    const std::string cert_file = PICO_TEST_CERT_DIR "/server.pem";
    const std::string key_file  = PICO_TEST_CERT_DIR "/server.key";

    asio::io_context io;
    pico::CoroServer server(io, 18197);
    server.use_tls(pico::ssl::make_server_context(cert_file, key_file));
    server.router().get("/hello", [](pico::Request&, pico::Response& res) {
        res = pico::Response::ok("Hello over HTTPS!");
    });
    server.run();

    std::thread t([&io]() { io.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::string resp = sync_https("127.0.0.1", 18197,
        "GET /hello HTTP/1.0\r\nHost: localhost\r\nConnection: close\r\n\r\n");

    io.stop();
    t.join();

    REQUIRE(resp.find("200 OK")           != std::string::npos);
    REQUIRE(resp.find("Hello over HTTPS!") != std::string::npos);
}

TEST_CASE("CoroServer: HTTPS POST echo", "[coro][https]") {
    const std::string cert_file = PICO_TEST_CERT_DIR "/server.pem";
    const std::string key_file  = PICO_TEST_CERT_DIR "/server.key";

    asio::io_context io;
    pico::CoroServer server(io, 18198);
    server.use_tls(pico::ssl::make_server_context(cert_file, key_file));
    server.router().post("/echo", [](pico::Request& req, pico::Response& res) {
        res = pico::Response::ok(req.body);
    });
    server.run();

    std::thread t([&io]() { io.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::string body = "secure-data";
    std::string request =
        "POST /echo HTTP/1.0\r\n"
        "Host: localhost\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" + body;

    std::string resp = sync_https("127.0.0.1", 18198, request);

    io.stop();
    t.join();

    REQUIRE(resp.find("200")        != std::string::npos);
    REQUIRE(resp.find("secure-data") != std::string::npos);
}

#endif // PICO_ENABLE_TLS
