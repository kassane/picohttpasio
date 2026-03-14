#include <catch2/catch_test_macros.hpp>
#include "static_files.hpp"
#include "server.hpp"
#include <asio.hpp>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include <string>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Create a temporary directory with test files; returns the path.
static std::filesystem::path make_test_root() {
    auto tmp = std::filesystem::temp_directory_path() / "pico_static_test";
    std::filesystem::create_directories(tmp);

    // index.html
    { std::ofstream f(tmp / "index.html"); f << "<html>index</html>"; }
    // style.css
    { std::ofstream f(tmp / "style.css"); f << "body{}"; }
    // data.json
    { std::ofstream f(tmp / "data.json"); f << R"({"ok":true})"; }
    // script.js
    { std::ofstream f(tmp / "script.js"); f << "console.log(1)"; }
    // image.png — minimal 1-byte binary
    { std::ofstream f(tmp / "image.png", std::ios::binary); f << '\x89'; }
    // subdir/page.html
    std::filesystem::create_directories(tmp / "subdir");
    { std::ofstream f(tmp / "subdir" / "page.html"); f << "<p>sub</p>"; }

    return tmp;
}

// Synchronous HTTP client helper (plain TCP)
static std::string sync_http(unsigned short port, const std::string& request) {
    using tcp = asio::ip::tcp;
    asio::io_context io;
    tcp::socket sock(io);
    tcp::resolver resolver(io);
    asio::connect(sock, resolver.resolve("127.0.0.1", std::to_string(port)));
    asio::write(sock, asio::buffer(request));
    std::string buf;
    std::error_code ec;
    asio::read(sock, asio::dynamic_buffer(buf), ec);
    return buf;
}

// ---------------------------------------------------------------------------
// Unit tests for serve_static() (no server, direct function calls)
// ---------------------------------------------------------------------------

TEST_CASE("serve_static: directory traversal rejected", "[static]") {
    auto root = make_test_root();
    pico::StaticConfig cfg;

    pico::Request req;
    req.method = pico::Method::GET;
    req.path   = "/../etc/passwd";
    auto res = pico::serve_static(req, root.string(), cfg);
    REQUIRE(static_cast<unsigned>(res.statusCode()) == 400);
}

TEST_CASE("serve_static: missing file returns 404", "[static]") {
    auto root = make_test_root();
    pico::Request req;
    req.method = pico::Method::GET;
    req.path   = "/nonexistent.txt";
    auto res = pico::serve_static(req, root.string());
    REQUIRE(static_cast<unsigned>(res.statusCode()) == 404);
}

TEST_CASE("serve_static: index.html served for /", "[static]") {
    auto root = make_test_root();
    pico::Request req;
    req.method = pico::Method::GET;
    req.path   = "/";
    auto res = pico::serve_static(req, root.string());
    REQUIRE(static_cast<unsigned>(res.statusCode()) == 200);
    REQUIRE(res.getBody().find("index") != std::string::npos);
    REQUIRE(res.getHeaders().count("Content-Type"));
    REQUIRE(res.getHeaders().at("Content-Type").find("text/html") != std::string::npos);
}

TEST_CASE("serve_static: MIME types detected correctly", "[static]") {
    auto root = make_test_root();

    auto mime_for = [&](const std::string& path) {
        pico::Request req;
        req.method = pico::Method::GET;
        req.path   = path;
        auto res = pico::serve_static(req, root.string());
        REQUIRE(static_cast<unsigned>(res.statusCode()) == 200);
        return res.getHeaders().count("Content-Type")
               ? res.getHeaders().at("Content-Type")
               : std::string{};
    };

    REQUIRE(mime_for("/style.css").find("text/css") != std::string::npos);
    REQUIRE(mime_for("/data.json").find("application/json") != std::string::npos);
    REQUIRE(mime_for("/script.js").find("application/javascript") != std::string::npos);
    REQUIRE(mime_for("/image.png").find("image/png") != std::string::npos);
}

TEST_CASE("serve_static: custom MIME type override", "[static]") {
    auto root = make_test_root();
    // Create a .wasm file
    { std::ofstream f(root / "app.wasm", std::ios::binary); f << "wasm"; }

    pico::StaticConfig cfg;
    cfg.extra_mime_types[".wasm"] = "application/wasm; charset=binary";

    pico::Request req;
    req.method = pico::Method::GET;
    req.path   = "/app.wasm";
    auto res = pico::serve_static(req, root.string(), cfg);
    REQUIRE(static_cast<unsigned>(res.statusCode()) == 200);
    REQUIRE(res.getHeaders().at("Content-Type").find("application/wasm") != std::string::npos);
}

TEST_CASE("serve_static: ETag and Last-Modified headers present", "[static]") {
    auto root = make_test_root();
    pico::StaticConfig cfg;
    cfg.caching = true;

    pico::Request req;
    req.method = pico::Method::GET;
    req.path   = "/index.html";
    auto res = pico::serve_static(req, root.string(), cfg);

    REQUIRE(static_cast<unsigned>(res.statusCode()) == 200);
    REQUIRE(res.getHeaders().count("ETag"));
    REQUIRE(res.getHeaders().count("Last-Modified"));
    REQUIRE(res.getHeaders().count("Cache-Control"));
}

TEST_CASE("serve_static: If-None-Match returns 304", "[static]") {
    auto root = make_test_root();
    pico::StaticConfig cfg;
    cfg.caching = true;

    // First request to get the ETag
    pico::Request req1;
    req1.method = pico::Method::GET;
    req1.path   = "/index.html";
    auto res1 = pico::serve_static(req1, root.string(), cfg);
    REQUIRE(static_cast<unsigned>(res1.statusCode()) == 200);
    auto etag = res1.getHeaders().at("ETag");

    // Second request with matching ETag
    pico::Request req2;
    req2.method          = pico::Method::GET;
    req2.path            = "/index.html";
    req2.headers["If-None-Match"] = etag;
    auto res2 = pico::serve_static(req2, root.string(), cfg);
    REQUIRE(static_cast<unsigned>(res2.statusCode()) == 304);
    REQUIRE(res2.getBody().empty());
}

TEST_CASE("serve_static: If-Modified-Since returns 304", "[static]") {
    auto root = make_test_root();
    pico::StaticConfig cfg;
    cfg.caching = true;

    pico::Request req1;
    req1.method = pico::Method::GET;
    req1.path   = "/index.html";
    auto res1 = pico::serve_static(req1, root.string(), cfg);
    REQUIRE(static_cast<unsigned>(res1.statusCode()) == 200);
    auto last_mod = res1.getHeaders().at("Last-Modified");

    pico::Request req2;
    req2.method                       = pico::Method::GET;
    req2.path                         = "/index.html";
    req2.headers["If-Modified-Since"] = last_mod;
    auto res2 = pico::serve_static(req2, root.string(), cfg);
    REQUIRE(static_cast<unsigned>(res2.statusCode()) == 304);
}

TEST_CASE("serve_static: Range request returns 206 Partial Content", "[static]") {
    auto root = make_test_root();
    // Write a known-content file
    { std::ofstream f(root / "range.txt"); f << "0123456789"; }

    pico::StaticConfig cfg;
    cfg.range_requests = true;

    pico::Request req;
    req.method          = pico::Method::GET;
    req.path            = "/range.txt";
    req.headers["Range"] = "bytes=2-5";
    auto res = pico::serve_static(req, root.string(), cfg);

    REQUIRE(static_cast<unsigned>(res.statusCode()) == 206);
    REQUIRE(res.getBody() == "2345");
    REQUIRE(res.getHeaders().count("Content-Range"));
    REQUIRE(res.getHeaders().at("Content-Range").find("bytes 2-5/10") != std::string::npos);
    REQUIRE(res.getHeaders().count("Accept-Ranges"));
}

TEST_CASE("serve_static: invalid Range returns 416", "[static]") {
    auto root = make_test_root();
    { std::ofstream f(root / "small.txt"); f << "hello"; }

    pico::StaticConfig cfg;
    cfg.range_requests = true;

    pico::Request req;
    req.method          = pico::Method::GET;
    req.path            = "/small.txt";
    req.headers["Range"] = "bytes=100-200";  // beyond file size
    auto res = pico::serve_static(req, root.string(), cfg);
    REQUIRE(static_cast<unsigned>(res.statusCode()) == 416);
}

TEST_CASE("serve_static: caching disabled — no ETag header", "[static]") {
    auto root = make_test_root();
    pico::StaticConfig cfg;
    cfg.caching = false;

    pico::Request req;
    req.method = pico::Method::GET;
    req.path   = "/index.html";
    auto res = pico::serve_static(req, root.string(), cfg);
    REQUIRE(static_cast<unsigned>(res.statusCode()) == 200);
    REQUIRE(res.getHeaders().find("ETag") == res.getHeaders().end());
}

TEST_CASE("serve_static: directory listing disabled returns 404 for bare dir", "[static]") {
    auto root = make_test_root();
    // Remove index.html from subdir so it's a bare directory
    std::filesystem::remove(root / "subdir" / "index.html");

    pico::StaticConfig cfg;
    cfg.directory_listing = false;

    pico::Request req;
    req.method = pico::Method::GET;
    req.path   = "/subdir";
    auto res = pico::serve_static(req, root.string(), cfg);
    REQUIRE(static_cast<unsigned>(res.statusCode()) == 404);
}

TEST_CASE("serve_static: directory listing enabled returns HTML", "[static]") {
    auto root = make_test_root();
    // Make a directory without index.html
    std::filesystem::create_directories(root / "listing_dir");
    { std::ofstream f(root / "listing_dir" / "file.txt"); f << "hi"; }

    pico::StaticConfig cfg;
    cfg.directory_listing = true;

    pico::Request req;
    req.method = pico::Method::GET;
    req.path   = "/listing_dir";
    auto res = pico::serve_static(req, root.string(), cfg);

    REQUIRE(static_cast<unsigned>(res.statusCode()) == 200);
    // Should contain the filename in the listing
    REQUIRE(res.getBody().find("file.txt") != std::string::npos);
}

TEST_CASE("serve_static: subdirectory file served correctly", "[static]") {
    auto root = make_test_root();
    pico::Request req;
    req.method = pico::Method::GET;
    req.path   = "/subdir/page.html";
    auto res = pico::serve_static(req, root.string());
    REQUIRE(static_cast<unsigned>(res.statusCode()) == 200);
    REQUIRE(res.getBody().find("<p>sub</p>") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Integration tests: HTTPServer with static file features (via HTTP)
// ---------------------------------------------------------------------------

TEST_CASE("HTTPServer: static file served via GET", "[integration][static]") {
    auto root = make_test_root();

    asio::io_context io;
    pico::HTTPServer server(io, 18280, root.string());

    std::thread t([&io]() { io.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::string resp = sync_http(18280,
        "GET / HTTP/1.0\r\nHost: localhost\r\nConnection: close\r\n\r\n");

    io.stop();
    t.join();

    REQUIRE(resp.find("200 OK")  != std::string::npos);
    REQUIRE(resp.find("index")   != std::string::npos);
}

TEST_CASE("HTTPServer: range request returns 206", "[integration][static]") {
    auto root = make_test_root();
    { std::ofstream f(root / "ten.txt"); f << "0123456789"; }

    asio::io_context io;
    pico::HTTPServer server(io, 18281, root.string());

    std::thread t([&io]() { io.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::string resp = sync_http(18281,
        "GET /ten.txt HTTP/1.0\r\n"
        "Host: localhost\r\n"
        "Range: bytes=0-3\r\n"
        "Connection: close\r\n\r\n");

    io.stop();
    t.join();

    REQUIRE(resp.find("206")       != std::string::npos);
    REQUIRE(resp.find("Content-Range") != std::string::npos);
    REQUIRE(resp.find("0123")      != std::string::npos);
}

TEST_CASE("HTTPServer: ETag conditional GET returns 304", "[integration][static]") {
    auto root = make_test_root();

    asio::io_context io;
    pico::HTTPServer server(io, 18282, root.string());

    std::thread t([&io]() { io.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // First request — get ETag
    std::string resp1 = sync_http(18282,
        "GET /index.html HTTP/1.0\r\nHost: localhost\r\nConnection: close\r\n\r\n");

    // Parse ETag from response
    auto etag_pos = resp1.find("ETag: ");
    REQUIRE(etag_pos != std::string::npos);
    auto etag_start = etag_pos + 6;
    auto etag_end   = resp1.find("\r\n", etag_start);
    std::string etag = resp1.substr(etag_start, etag_end - etag_start);

    // Second request with If-None-Match
    std::string req2 =
        "GET /index.html HTTP/1.0\r\n"
        "Host: localhost\r\n"
        "If-None-Match: " + etag + "\r\n"
        "Connection: close\r\n\r\n";

    // Need a fresh connection since we used HTTP/1.0 Connection: close
    std::string resp2 = sync_http(18282, req2);

    io.stop();
    t.join();

    REQUIRE(resp2.find("304") != std::string::npos);
}

// ---------------------------------------------------------------------------
// HTTPServer TLS test (only when PICO_ENABLE_TLS)
// ---------------------------------------------------------------------------

#ifdef PICO_ENABLE_TLS
#include "ssl_context.hpp"
#include <asio/ssl.hpp>

static std::string sync_https(unsigned short port, const std::string& request) {
    using tcp = asio::ip::tcp;
    asio::io_context io;
    asio::ssl::context ssl_ctx(asio::ssl::context::tls_client);
    ssl_ctx.set_verify_mode(asio::ssl::verify_none);

    asio::ssl::stream<tcp::socket> ssl_sock(io, ssl_ctx);
    tcp::resolver resolver(io);
    asio::connect(ssl_sock.lowest_layer(), resolver.resolve("127.0.0.1", std::to_string(port)));
    ssl_sock.handshake(asio::ssl::stream_base::client);

    asio::write(ssl_sock, asio::buffer(request));
    std::string buf;
    std::error_code ec;
    asio::read(ssl_sock, asio::dynamic_buffer(buf), ec);
    return buf;
}

TEST_CASE("HTTPServer: TLS HTTPS GET returns 200", "[integration][tls]") {
    const std::string cert_file = PICO_TEST_CERT_DIR "/server.pem";
    const std::string key_file  = PICO_TEST_CERT_DIR "/server.key";

    auto root = make_test_root();

    asio::io_context io;
    pico::HTTPServer server(io, 18290, root.string());
    server.use_tls(pico::ssl::make_server_context(cert_file, key_file));
    server.router().get("/hello", [](pico::Request&, pico::Response& res) {
        res = pico::Response::ok("Hello TLS from HTTPServer!");
    });

    std::thread t([&io]() { io.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::string resp = sync_https(18290,
        "GET /hello HTTP/1.0\r\nHost: localhost\r\nConnection: close\r\n\r\n");

    io.stop();
    t.join();

    REQUIRE(resp.find("200 OK")                    != std::string::npos);
    REQUIRE(resp.find("Hello TLS from HTTPServer!") != std::string::npos);
}

TEST_CASE("HTTPServer: TLS HTTPS static file served", "[integration][tls]") {
    const std::string cert_file = PICO_TEST_CERT_DIR "/server.pem";
    const std::string key_file  = PICO_TEST_CERT_DIR "/server.key";

    auto root = make_test_root();

    asio::io_context io;
    pico::HTTPServer server(io, 18291, root.string());
    server.use_tls(pico::ssl::make_server_context(cert_file, key_file));

    std::thread t([&io]() { io.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::string resp = sync_https(18291,
        "GET / HTTP/1.0\r\nHost: localhost\r\nConnection: close\r\n\r\n");

    io.stop();
    t.join();

    REQUIRE(resp.find("200 OK") != std::string::npos);
    REQUIRE(resp.find("index")  != std::string::npos);
}

#endif // PICO_ENABLE_TLS
