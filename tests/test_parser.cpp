#include <catch2/catch_test_macros.hpp>
#include "picohttpwrapper.hpp"

// ---------------------------------------------------------------------------
// HTTPRequest tests
// ---------------------------------------------------------------------------

TEST_CASE("HTTPRequest parses a minimal GET request", "[parser][request]") {
    std::string raw = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
    HTTPRequest req(raw);
    REQUIRE(req.parse());
    REQUIRE(req.getMethod() == "GET");
    REQUIRE(req.getPath()   == "/");
    REQUIRE(req.getMinorVersion() == 1);
    REQUIRE(req.getHeader("Host") == "localhost");
}

TEST_CASE("HTTPRequest parses POST with multiple headers", "[parser][request]") {
    std::string raw =
        "POST /submit HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 2\r\n"
        "\r\n";
    HTTPRequest req(raw);
    REQUIRE(req.parse());
    REQUIRE(req.getMethod() == "POST");
    REQUIRE(req.getPath()   == "/submit");
    REQUIRE(req.getHeader("Content-Type")   == "application/json");
    REQUIRE(req.getHeader("Content-Length") == "2");
}

TEST_CASE("HTTPRequest rejects malformed input", "[parser][request]") {
    HTTPRequest req("NOTHTTP\r\n");
    REQUIRE_FALSE(req.parse());
}

TEST_CASE("HTTPRequest returns empty string for missing header", "[parser][request]") {
    std::string raw = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
    HTTPRequest req(raw);
    REQUIRE(req.parse());
    REQUIRE(req.getHeader("X-Missing").empty());
}

TEST_CASE("HTTPRequest separates path from query string", "[parser][request]") {
    std::string raw = "GET /search?q=hello&page=2 HTTP/1.1\r\nHost: x\r\n\r\n";
    HTTPRequest req(raw);
    REQUIRE(req.parse());
    REQUIRE(req.getPath()  == "/search");
    REQUIRE(req.getQuery() == "q=hello&page=2");
}

TEST_CASE("HTTPRequest::queryParams parses key-value pairs", "[parser][request]") {
    std::string raw = "GET /x?foo=bar&baz=qux HTTP/1.1\r\nHost: x\r\n\r\n";
    HTTPRequest req(raw);
    REQUIRE(req.parse());
    auto qp = req.queryParams();
    REQUIRE(qp.at("foo") == "bar");
    REQUIRE(qp.at("baz") == "qux");
}

TEST_CASE("HTTPRequest::queryParams handles URL-encoded values", "[parser][request]") {
    std::string raw = "GET /x?name=hello%20world&plus=a+b HTTP/1.1\r\nHost: x\r\n\r\n";
    HTTPRequest req(raw);
    REQUIRE(req.parse());
    auto qp = req.queryParams();
    REQUIRE(qp.at("name") == "hello world");
    REQUIRE(qp.at("plus") == "a b");
}

TEST_CASE("HTTPRequest::queryParams handles value-less keys", "[parser][request]") {
    std::string raw = "GET /x?flag HTTP/1.1\r\nHost: x\r\n\r\n";
    HTTPRequest req(raw);
    REQUIRE(req.parse());
    auto qp = req.queryParams();
    REQUIRE(qp.count("flag") == 1);
    REQUIRE(qp.at("flag").empty());
}

TEST_CASE("HTTPRequest parses HTTP/1.0", "[parser][request]") {
    std::string raw = "GET / HTTP/1.0\r\nHost: x\r\n\r\n";
    HTTPRequest req(raw);
    REQUIRE(req.parse());
    REQUIRE(req.getMinorVersion() == 0);
}

// ---------------------------------------------------------------------------
// HTTPResponse tests
// ---------------------------------------------------------------------------

TEST_CASE("HTTPResponse parses 200 OK", "[parser][response]") {
    std::string raw =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: 5\r\n"
        "\r\n";
    HTTPResponse resp(raw);
    REQUIRE(resp.parse());
    REQUIRE(resp.statusCode()    == 200);
    REQUIRE(resp.statusMessage() == "OK");
    REQUIRE(resp.minorVersion()  == 1);
    REQUIRE(resp.header("Content-Type")   == "text/html");
    REQUIRE(resp.header("Content-Length") == "5");
}

TEST_CASE("HTTPResponse parses 404 Not Found", "[parser][response]") {
    std::string raw = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
    HTTPResponse resp(raw);
    REQUIRE(resp.parse());
    REQUIRE(resp.statusCode()    == 404);
    REQUIRE(resp.statusMessage() == "Not Found");
}

TEST_CASE("HTTPResponse parses 101 Switching Protocols", "[parser][response]") {
    std::string raw =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "\r\n";
    HTTPResponse resp(raw);
    REQUIRE(resp.parse());
    REQUIRE(resp.statusCode() == 101);
    REQUIRE(resp.header("Upgrade") == "websocket");
}

TEST_CASE("HTTPResponse rejects malformed input", "[parser][response]") {
    HTTPResponse resp("GARBAGE\r\n");
    REQUIRE_FALSE(resp.parse());
}

TEST_CASE("HTTPResponse returns empty string for missing header", "[parser][response]") {
    std::string raw = "HTTP/1.1 200 OK\r\n\r\n";
    HTTPResponse resp(raw);
    REQUIRE(resp.parse());
    REQUIRE(resp.header("X-Missing").empty());
}

// ---------------------------------------------------------------------------
// url_decode static helper
// ---------------------------------------------------------------------------
TEST_CASE("url_decode handles percent-encoding", "[parser]") {
    REQUIRE(HTTPRequest::url_decode("%41%42%43") == "ABC");
    REQUIRE(HTTPRequest::url_decode("hello%20world") == "hello world");
    REQUIRE(HTTPRequest::url_decode("no+encoding") == "no encoding");
    REQUIRE(HTTPRequest::url_decode("plain") == "plain");
    REQUIRE(HTTPRequest::url_decode("") == "");
}
