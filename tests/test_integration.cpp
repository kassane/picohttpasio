#include <catch2/catch_test_macros.hpp>
#include "server.hpp"
#include <asio.hpp>
#include <thread>
#include <chrono>
#include <atomic>
#include <string>

// ---------------------------------------------------------------------------
// Synchronous HTTP helper: connect, write request, read until EOF.
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
// Tests use distinct port numbers to avoid binding conflicts.
// ---------------------------------------------------------------------------

TEST_CASE("Server: GET /hello returns 200 OK", "[integration]") {
    asio::io_context io;
    pico::HTTPServer server(io, 18080);
    server.router().get("/hello", [](pico::Request&, pico::Response& res) {
        res = pico::Response::ok("Hello, World!");
    });

    std::thread t([&io]() { io.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::string resp = sync_http("127.0.0.1", 18080,
        "GET /hello HTTP/1.0\r\nHost: localhost\r\nConnection: close\r\n\r\n");

    io.stop();
    t.join();

    REQUIRE(resp.find("200 OK")        != std::string::npos);
    REQUIRE(resp.find("Hello, World!") != std::string::npos);
}

TEST_CASE("Server: unmatched route returns 404", "[integration]") {
    asio::io_context io;
    pico::HTTPServer server(io, 18081);
    // No routes registered

    std::thread t([&io]() { io.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::string resp = sync_http("127.0.0.1", 18081,
        "GET /missing HTTP/1.0\r\nHost: localhost\r\nConnection: close\r\n\r\n");

    io.stop();
    t.join();

    REQUIRE(resp.find("404") != std::string::npos);
}

TEST_CASE("Server: path params extracted and echoed", "[integration]") {
    asio::io_context io;
    pico::HTTPServer server(io, 18082);
    server.router().get("/users/:id", [](pico::Request& req, pico::Response& res) {
        res = pico::Response::ok("user:" + std::string(req.param("id")));
    });

    std::thread t([&io]() { io.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::string resp = sync_http("127.0.0.1", 18082,
        "GET /users/42 HTTP/1.0\r\nHost: localhost\r\nConnection: close\r\n\r\n");

    io.stop();
    t.join();

    REQUIRE(resp.find("200")     != std::string::npos);
    REQUIRE(resp.find("user:42") != std::string::npos);
}

TEST_CASE("Server: POST body is received by handler", "[integration]") {
    asio::io_context io;
    pico::HTTPServer server(io, 18083);
    server.router().post("/echo", [](pico::Request& req, pico::Response& res) {
        res = pico::Response::ok(req.body);
    });

    std::thread t([&io]() { io.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::string body = "hello-body";
    std::string request =
        "POST /echo HTTP/1.0\r\n"
        "Host: localhost\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" + body;

    std::string resp = sync_http("127.0.0.1", 18083, request);

    io.stop();
    t.join();

    REQUIRE(resp.find("200")        != std::string::npos);
    REQUIRE(resp.find("hello-body") != std::string::npos);
}

TEST_CASE("Server: keep-alive serves multiple requests", "[integration]") {
    asio::io_context io;
    std::atomic<int> hit_count{0};
    pico::HTTPServer server(io, 18084);
    server.router().get("/ping", [&hit_count](pico::Request&, pico::Response& res) {
        ++hit_count;
        res = pico::Response::ok("pong");
    });

    std::thread t([&io]() { io.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Send two requests over one TCP connection
    using tcp = asio::ip::tcp;
    asio::io_context cio;
    tcp::socket sock(cio);
    tcp::resolver rslv(cio);
    asio::connect(sock, rslv.resolve("127.0.0.1", "18084"));

    std::string req1 = "GET /ping HTTP/1.1\r\nHost: localhost\r\nConnection: keep-alive\r\n\r\n";
    std::string req2 = "GET /ping HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    asio::write(sock, asio::buffer(req1 + req2));

    std::string buf;
    std::error_code ec;
    asio::read(sock, asio::dynamic_buffer(buf), ec);

    io.stop();
    t.join();

    REQUIRE(hit_count >= 2);
    // Both responses must contain 200 OK
    size_t count = 0, pos = 0;
    while ((pos = buf.find("200 OK", pos)) != std::string::npos) { ++count; ++pos; }
    REQUIRE(count >= 2);
}

TEST_CASE("Server: middleware runs before handler", "[integration]") {
    asio::io_context io;
    std::atomic<bool> mw_ran{false};

    pico::HTTPServer server(io, 18085);
    server.router().use([&mw_ran](pico::Request&, pico::Response&, std::function<void()> next) {
        mw_ran = true;
        next();
    });
    server.router().get("/x", [](pico::Request&, pico::Response& res) {
        res = pico::Response::ok("ok");
    });

    std::thread t([&io]() { io.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    sync_http("127.0.0.1", 18085,
        "GET /x HTTP/1.0\r\nHost: localhost\r\nConnection: close\r\n\r\n");

    io.stop();
    t.join();

    REQUIRE(mw_ran.load());
}

TEST_CASE("Server: malformed request returns 400", "[integration]") {
    asio::io_context io;
    pico::HTTPServer server(io, 18086);

    std::thread t([&io]() { io.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::string resp = sync_http("127.0.0.1", 18086,
        "NOTHTTP garbage\r\n\r\n");

    io.stop();
    t.join();

    REQUIRE(resp.find("400") != std::string::npos);
}
