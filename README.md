# picohttpasio

A lightweight, user-friendly C++23 HTTP/1.1 SDK built on
[standalone ASIO](https://github.com/chriskohlhoff/asio) and
[picohttpparser](https://github.com/h2o/picohttpparser).

An accessible alternative to boost::beast — combining beast's async ASIO foundation
with an Express-style developer experience.

## Features

- **Express-like router** — path parameters (`/users/:id`), wildcards (`/static/*`),
  HTTP method dispatch (GET, POST, PUT, DELETE, PATCH, …)
- **Middleware chain** — global or prefix-scoped; short-circuit or pass-through
- **Fluent Response builder** — status, headers, body, JSON, HTML, redirect
- **Keep-alive / HTTP pipelining** — reuse connections for multiple requests
- **WebSocket upgrade** — `Upgrade: websocket` handled automatically; RFC-compliant
  handshake (inline SHA-1, no OpenSSL dependency) with full frame codec
- **Static file serving** — content-type detection, directory-traversal protection
- **Async client** — `RequestBuilder` + keep-alive connection reuse, bug-free response parsing
- **Catch2 v3 test suite** — unit tests + localhost integration tests

## Prerequisites

- C++23 compiler (GCC 13+, Clang 16+, MSVC 19.35+)
- CMake 3.14+
- Internet access for first build (FetchContent downloads ASIO + Catch2 automatically)

## Building

```sh
git clone <repository-url>
cd picohttpasio

cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel

# Run tests
ctest --test-dir build --output-on-failure
```

### Release build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### Zig build (alternative)

```sh
zig build run -Doptimize=ReleaseFast
```

## Quick Start — Server

```cpp
#include <server.hpp>

int main() {
    asio::io_context io;
    pico::HTTPServer server(io, 8080, "./www");  // static files from ./www

    // Middleware
    server.router().use(pico::logging_middleware());
    server.router().use(pico::cors_middleware());

    // Routes
    server.router()
        .get("/hello", [](pico::Request&, pico::Response& res) {
            res = pico::Response::ok("Hello, World!\n");
        })
        .get("/users/:id", [](pico::Request& req, pico::Response& res) {
            res = pico::Response::make()
                .json(R"({"id":")" + std::string(req.param("id")) + R"("})");
        })
        .post("/echo", [](pico::Request& req, pico::Response& res) {
            res = pico::Response::ok(req.body);
        });

    // WebSocket upgrade
    server.on_websocket([](std::shared_ptr<pico::ws::WebSocketConnection> conn) {
        conn->on_message([conn](pico::ws::Frame f) {
            conn->send(f.payload);  // echo
        });
    });

    io.run();
}
```

## Quick Start — Client

```cpp
#include <client.hpp>

int main() {
    asio::io_context io;
    pico::HTTPClient client(io, "httpbin.org", 80);

    auto req = pico::RequestBuilder{}
        .get("/get")
        .host("httpbin.org")
        .keep_alive(false)
        .build();

    client.request(req, [](std::optional<HTTPResponse> resp) {
        if (resp) {
            std::cout << resp->statusCode() << " " << resp->statusMessage() << "\n";
        }
    });

    io.run();
}
```

## API Reference

### `pico::Response` builder

```cpp
pico::Response::ok("body")
pico::Response::not_found()
pico::Response::bad_request("msg")
pico::Response::no_content()
pico::Response::redirect("/new-url")
pico::Response::make(pico::StatusCode::Created)
    .header("X-Custom", "value")
    .json(R"({"ok":true})")
    .serialize()  // → HTTP/1.1 wire string
```

### `pico::Router`

```cpp
router.get   ("/path",         handler)
router.post  ("/path",         handler)
router.put   ("/path/:id",     handler)
router.del   ("/path/:id",     handler)
router.patch ("/path/:id",     handler)
router.any   ("/path",         handler)   // all methods
router.use   (middleware)                  // global
router.use   ("/prefix", middleware)       // prefix-scoped
```

### `pico::Request` (inside handlers)

```cpp
req.method          // pico::Method::GET / POST / …
req.path            // "/users/42"
req.body            // request body string
req.header("Host")  // header value (string_view)
req.param("id")     // path parameter (string_view)
req.query_param("q")// query parameter (string_view)
req.keepAlive()     // bool
```

### `pico::RequestBuilder`

```cpp
pico::RequestBuilder{}
    .get("/path")
    .host("example.com")
    .header("Authorization", "Bearer token")
    .keep_alive(true)
    .build()  // → wire-format string
```

### WebSocket (`pico::ws`)

```cpp
pico::ws::ws_accept_key(client_key)   // handshake key
pico::ws::encode(frame)               // frame → bytes
pico::ws::decode(buffer, frame_out)   // bytes → frame (0 = incomplete)

// WebSocketConnection
conn->send("hello")
conn->send(data, pico::ws::Opcode::Binary)
conn->close()
conn->on_message([](pico::ws::Frame f) { … })
conn->on_close([]() { … })
```

### Built-in middleware

```cpp
pico::logging_middleware()         // prints METHOD path -> STATUS to stderr
pico::cors_middleware("*")         // CORS headers (permissive default)
```

## Project Layout

```
include/
  http_types.hpp      Method enum, StatusCode enum
  picohttpwrapper.hpp HTTPRequest + HTTPResponse parsers (picohttpparser wrapper)
  request.hpp         Request value type + parse_request() factory
  response.hpp        Response fluent builder + serializer
  router.hpp          Router, path matching, middleware chain
  websocket.hpp       WS handshake (SHA-1), frame codec, WebSocketConnection
  server.hpp          HTTPServer (keep-alive, WS dispatch, static files)
  client.hpp          HTTPClient + RequestBuilder
src/
  picohttpparser.c    H2O picohttpparser (unchanged)
  main.cpp            Demo: server + client
tests/
  test_types.cpp      Method/StatusCode unit tests
  test_parser.cpp     Request/response parser tests
  test_response.cpp   Response builder + Request factory tests
  test_router.cpp     Routing, path params, middleware tests
  test_websocket.cpp  SHA-1 key, frame encode/decode tests
  test_integration.cpp Real localhost server+client round-trip tests
examples/
  simple_server.cpp   Basic server with routes and middleware
  rest_api.cpp        CRUD REST API demo
  websocket_echo.cpp  WebSocket echo server
```

## Comparison with boost::beast

| Feature                        | boost::beast     | picohttpasio       |
|-------------------------------|------------------|--------------------|
| Routing                       | Manual           | Built-in router    |
| Middleware                    | None             | Chain system       |
| Response builder              | Manual           | Fluent API         |
| WebSocket                     | Yes (low-level)  | Yes (high-level)   |
| Keep-alive                    | Manual           | Automatic          |
| Boost dependency              | Required         | None               |
| C++ standard                  | C++11+           | C++23              |
| Learning curve                | Steep            | Express-like       |

## License

MIT License. See [LICENSE](LICENSE) for details.

### Acknowledgments

- [picohttpparser](https://github.com/h2o/picohttpparser): Kazuho Oku, Tokuhiro Matsuno, Daisuke Murase, Shigeo Mitsunari (MIT)
- [ASIO](https://github.com/chriskohlhoff/asio): Christopher M. Kohlhoff (BSL-1.0)
- [Catch2](https://github.com/catchorg/Catch2): Martin Hořeňovský et al. (BSL-1.0)
