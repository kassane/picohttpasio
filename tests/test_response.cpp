#include <catch2/catch_test_macros.hpp>
#include "response.hpp"
#include "request.hpp"
#include "picohttpwrapper.hpp"
#include <string>
#include <sstream>

using namespace pico;

// ---------------------------------------------------------------------------
// Response builder tests
// ---------------------------------------------------------------------------

TEST_CASE("Response::ok produces 200 with empty body", "[response]") {
    auto r = Response::ok();
    REQUIRE(r.statusCode() == StatusCode::OK);
    REQUIRE(r.getBody().empty());
}

TEST_CASE("Response::ok with body sets Content-Length correctly", "[response]") {
    auto r = Response::ok("hello");
    auto wire = r.serialize();
    REQUIRE(wire.find("HTTP/1.1 200 OK") != std::string::npos);
    REQUIRE(wire.find("Content-Length: 5") != std::string::npos);
    // Body appears after blank line
    auto sep = wire.find("\r\n\r\n");
    REQUIRE(sep != std::string::npos);
    REQUIRE(wire.substr(sep + 4) == "hello");
}

TEST_CASE("Response::not_found produces 404", "[response]") {
    auto r = Response::not_found();
    REQUIRE(r.statusCode() == StatusCode::NotFound);
    REQUIRE(r.serialize().find("HTTP/1.1 404 Not Found") != std::string::npos);
}

TEST_CASE("Response::bad_request produces 400", "[response]") {
    auto r = Response::bad_request("oops");
    REQUIRE(r.statusCode() == StatusCode::BadRequest);
    auto wire = r.serialize();
    REQUIRE(wire.find("400 Bad Request") != std::string::npos);
    REQUIRE(wire.find("oops") != std::string::npos);
}

TEST_CASE("Response::method_not_allowed produces 405", "[response]") {
    auto r = Response::method_not_allowed();
    REQUIRE(r.statusCode() == StatusCode::MethodNotAllowed);
}

TEST_CASE("Response::json sets Content-Type header", "[response]") {
    auto r = Response::make().json(R"({"key":"value"})");
    auto wire = r.serialize();
    REQUIRE(wire.find("Content-Type: application/json") != std::string::npos);
}

TEST_CASE("Response::html sets Content-Type header", "[response]") {
    auto r = Response::make().html("<h1>Hi</h1>");
    auto wire = r.serialize();
    REQUIRE(wire.find("Content-Type: text/html") != std::string::npos);
}

TEST_CASE("Response::redirect sets Location header and 302", "[response]") {
    auto r = Response::redirect("/new-path");
    REQUIRE(r.statusCode() == StatusCode::Found);
    auto wire = r.serialize();
    REQUIRE(wire.find("Location: /new-path") != std::string::npos);
    REQUIRE(wire.find("302 Found") != std::string::npos);
}

TEST_CASE("Response::redirect supports custom status code", "[response]") {
    auto r = Response::redirect("/perm", StatusCode::MovedPermanently);
    REQUIRE(r.statusCode() == StatusCode::MovedPermanently);
    REQUIRE(r.serialize().find("301 Moved Permanently") != std::string::npos);
}

TEST_CASE("Response fluent chain sets multiple custom headers", "[response]") {
    auto r = Response::make()
        .status(StatusCode::Created)
        .header("X-Custom", "foo")
        .header("X-Other",  "bar")
        .body("ok");
    auto wire = r.serialize();
    REQUIRE(wire.find("201 Created")   != std::string::npos);
    REQUIRE(wire.find("X-Custom: foo") != std::string::npos);
    REQUIRE(wire.find("X-Other: bar")  != std::string::npos);
}

TEST_CASE("Response always includes Content-Length (even for 0)", "[response]") {
    auto r = Response::no_content();
    auto wire = r.serialize();
    REQUIRE(wire.find("Content-Length: 0") != std::string::npos);
}

TEST_CASE("Response::switching_protocols includes WS headers", "[response]") {
    auto r = Response::switching_protocols("dGhlIHNhbXBsZSBub25jZQ==");
    auto wire = r.serialize();
    REQUIRE(wire.find("101 Switching Protocols")           != std::string::npos);
    REQUIRE(wire.find("Upgrade: websocket")                != std::string::npos);
    REQUIRE(wire.find("Connection: Upgrade")               != std::string::npos);
    REQUIRE(wire.find("Sec-WebSocket-Accept: dGhlIHNhbX") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Request (parse_request factory) tests
// ---------------------------------------------------------------------------

TEST_CASE("parse_request returns valid Request for GET", "[request]") {
    std::string raw = "GET /path?k=v HTTP/1.1\r\nHost: localhost\r\n\r\n";
    auto req = parse_request(raw);
    REQUIRE(req.has_value());
    REQUIRE(req->method      == Method::GET);
    REQUIRE(req->path        == "/path");
    REQUIRE(req->query       == "k=v");
    REQUIRE(req->httpVersion == 1);
    REQUIRE(req->header("Host") == "localhost");
    REQUIRE(req->query_param("k") == "v");
}

TEST_CASE("parse_request returns nullopt for invalid input", "[request]") {
    auto req = parse_request("GARBAGE");
    REQUIRE_FALSE(req.has_value());
}

TEST_CASE("parse_request keeps-alive on HTTP/1.1 by default", "[request]") {
    std::string raw = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
    auto req = parse_request(raw);
    REQUIRE(req.has_value());
    REQUIRE(req->keepAlive());
}

TEST_CASE("parse_request detects Connection: close", "[request]") {
    std::string raw = "GET / HTTP/1.1\r\nConnection: close\r\n\r\n";
    auto req = parse_request(raw);
    REQUIRE(req.has_value());
    REQUIRE_FALSE(req->keepAlive());
}

TEST_CASE("parse_request HTTP/1.0 defaults to no keep-alive", "[request]") {
    std::string raw = "GET / HTTP/1.0\r\nHost: x\r\n\r\n";
    auto req = parse_request(raw);
    REQUIRE(req.has_value());
    REQUIRE_FALSE(req->keepAlive());
}

TEST_CASE("parse_request carries body", "[request]") {
    std::string raw = "POST /x HTTP/1.1\r\nContent-Length: 3\r\n\r\n";
    auto req = parse_request(raw, "abc");
    REQUIRE(req.has_value());
    REQUIRE(req->body == "abc");
}
