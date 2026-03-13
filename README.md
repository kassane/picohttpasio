# picohttpasio

[![CI](https://github.com/kassane/picohttpasio/actions/workflows/ci.yml/badge.svg)](https://github.com/kassane/picohttpasio/actions/workflows/ci.yml)

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
- **WebSocket** — handled automatically; RFC-compliant
  handshake (inline SHA-1) with full frame codec
- **HTTPS/TLS** — optional; wraps ASIO SSL with OpenSSL (enabled by `-DPICO_ENABLE_TLS=ON`)
- **Crypto primitives** — SHA-256/512, BLAKE2b, Ed25519 signatures, AES-256-GCM AEAD,
  X25519 key exchange; all via OpenSSL EVP (no extra dependency beyond TLS)
- **Static file serving** — content-type detection, directory-traversal protection
- **Async client** — `RequestBuilder` + keep-alive connection reuse
- **Cross-platform** — Linux, macOS, Windows (MSVC 19.35+ / GCC 13+ / Clang 16+)
- **Catch2 v3 test suite** — unit tests + localhost integration tests

## Prerequisites

| | Required | Optional |
|---|---|---|
| Compiler | C++23 (GCC 13+, Clang 16+, MSVC 19.35+) | |
| Build | CMake 3.14+ | |
| TLS / Crypto | — | OpenSSL 1.1.1+ (`libssl-dev` on Ubuntu, `brew install openssl` on macOS) |
| Network | First build fetches ASIO + Catch2 via FetchContent | |

## Using picohttpasio in your project (FetchContent)

Add the following to your `CMakeLists.txt`:

```cmake
include(FetchContent)

FetchContent_Declare(picohttpasio
    GIT_REPOSITORY https://github.com/kassane/picohttpasio.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(picohttpasio)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE picohttpasio)
```

Enable TLS/crypto support (requires OpenSSL):

```cmake
set(PICO_ENABLE_TLS ON CACHE BOOL "" FORCE)
FetchContent_Declare(picohttpasio ...)
FetchContent_MakeAvailable(picohttpasio)
```

## Building (tests & examples)

```sh
git clone https://github.com/kassane/picohttpasio
cd picohttpasio

# Default (TLS on — requires OpenSSL)
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure

# Without TLS/crypto
cmake -B build -DPICO_ENABLE_TLS=OFF
cmake --build build --parallel
```

### macOS

```sh
brew install openssl
cmake -B build -DOPENSSL_ROOT_DIR=$(brew --prefix openssl)
cmake --build build --parallel
```

### Windows (MSVC)

```bat
choco install openssl
cmake -B build -DOPENSSL_ROOT_DIR="C:/Program Files/OpenSSL-Win64"
cmake --build build --parallel
```

### Release build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

## Continuous Integration

CI runs on every push via GitHub Actions across a full platform × TLS matrix:

| Platform | TLS ON | TLS OFF |
|---|---|---|
| Ubuntu (GCC) | ✓ | ✓ |
| macOS (Clang) | ✓ | ✓ |
| Windows (MSVC) | ✓ | ✓ |

A separate benchmark workflow runs [TechEmpower](https://www.techempower.com/benchmarks/)-style
endpoints (Plaintext cat. 6 + JSON cat. 1) with `wrk` on each push to main.

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

## Quick Start — HTTPS Server

```cpp
#include <coro_server.hpp>   // requires PICO_ENABLE_TLS

int main() {
    asio::io_context io;
    asio::ssl::context ssl(asio::ssl::context::tls);
    ssl.use_certificate_chain_file("cert.pem");
    ssl.use_private_key_file("key.pem", asio::ssl::context::pem);

    pico::CoroServer server(io, ssl, 8443);
    server.router()
        .get("/", [](pico::Request&, pico::Response& res) {
            res = pico::Response::ok("Hello over TLS!\n");
        });

    io.run();
}
```

## Quick Start — Crypto

Requires `PICO_ENABLE_TLS=ON`. All primitives are backed by OpenSSL EVP.

```cpp
#include <crypto.hpp>

// Hashing
auto h256  = pico::crypto::sha256("hello");           // SHA-256 → 32 bytes
auto h512  = pico::crypto::sha512("hello");           // SHA-512 → 64 bytes
auto hb2b  = pico::crypto::blake2b("hello");          // BLAKE2b-512 → 64 bytes
auto hb2bk = pico::crypto::blake2b("hello", "key");  // HMAC-SHA-512 keyed variant

std::string hex = pico::crypto::to_hex(h256);         // → "2cf24dba..."

// Secure random bytes
auto bytes  = pico::crypto::random_bytes(32);
auto secret = pico::crypto::random_string(16);

// AES-256-GCM symmetric AEAD
auto key = pico::crypto::secretbox::generate_key();   // 32-byte key
auto ct  = pico::crypto::secretbox::encrypt("hello", key);  // IV+tag+ciphertext
auto pt  = pico::crypto::secretbox::decrypt(ct, key); // → optional<string>

// X25519 + AES-256-GCM public-key AEAD
auto alice = pico::crypto::box::generate_keypair();
auto bob   = pico::crypto::box::generate_keypair();
auto enc   = pico::crypto::box::encrypt("secret", bob.pk, alice.sk);
auto dec   = pico::crypto::box::decrypt(enc, alice.pk, bob.sk);

// Ed25519 digital signatures
auto kp  = pico::crypto::sign::generate_keypair();
auto sig = pico::crypto::sign::sign_detached("message", kp.sk);
bool ok  = pico::crypto::sign::verify_detached("message", sig, kp.pk);
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

## Benchmark

`examples/techempower_bench.cpp` implements
[TechEmpower Web Framework Benchmark](https://www.techempower.com/benchmarks/#section=code)
categories:

| Category | Endpoint | Description |
|---|---|---|
| 1 — JSON | `GET /json` | `{"message":"Hello, World!"}` |
| 6 — Plaintext | `GET /plaintext` | `Hello, World!` |

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --parallel
./build/examples/techempower_bench &
wrk -t4 -c128 -d10s --latency http://localhost:8080/plaintext
wrk -t4 -c128 -d10s --latency http://localhost:8080/json
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
router.del   ("/path/:id",     handler)   // HTTP DELETE (Method::DEL)
router.patch ("/path/:id",     handler)
router.any   ("/path",         handler)   // all methods
router.use   (middleware)                  // global
router.use   ("/prefix", middleware)       // prefix-scoped
```

> **Note:** The C++ enum value is `pico::Method::DEL` (not `DELETE`) to avoid
> a collision with the `DELETE` macro defined in `<winnt.h>` on Windows.
> The HTTP verb string `"DELETE"` is used in wire format and string conversions.

### `pico::Request` (inside handlers)

```cpp
req.method          // pico::Method::GET / POST / DEL / …
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
  http_types.hpp        Method enum (DEL = HTTP DELETE), StatusCode enum
  picohttpwrapper.hpp   HTTPRequest + HTTPResponse parsers (picohttpparser wrapper)
  request.hpp           Request value type + parse_request() factory
  response.hpp          Response fluent builder + serializer
  router.hpp            Router, path matching, middleware chain
  websocket.hpp         WS handshake (SHA-1), frame codec, WebSocketConnection
  server.hpp            HTTPServer (keep-alive, WS dispatch, static files)
  coro_server.hpp       Coroutine-based HTTPS server (requires PICO_ENABLE_TLS)
  client.hpp            HTTPClient + RequestBuilder
  crypto.hpp            Crypto primitives via OpenSSL EVP (requires PICO_ENABLE_TLS)
src/
  (picohttpparser fetched automatically via FetchContent)
tests/
  test_types.cpp        Method/StatusCode unit tests
  test_parser.cpp       Request/response parser tests
  test_response.cpp     Response builder + Request factory tests
  test_router.cpp       Routing, path params, middleware tests
  test_websocket.cpp    SHA-1 key, frame encode/decode tests
  test_integration.cpp  Real localhost server+client round-trip tests
  test_crypto.cpp       Crypto primitives tests (requires PICO_ENABLE_TLS)
  test_coro.cpp         TLS coroutine server tests (requires PICO_ENABLE_TLS)
examples/
  simple_server.cpp     Basic server with routes and middleware
  rest_api.cpp          CRUD REST API demo
  websocket_echo.cpp    WebSocket echo server
  coro_server.cpp       Coroutine HTTPS server demo
  https_server.cpp      Full HTTPS + WebSocket + crypto demo
  techempower_bench.cpp TechEmpower benchmark server (cat. 1 JSON + cat. 6 Plaintext)
.github/workflows/
  ci.yml                Build + test matrix: Linux / macOS / Windows × TLS ON/OFF
  bench.yml             TechEmpower-style wrk benchmark (Linux, informational)
```

## Comparison with boost::beast

| Feature                        | boost::beast     | picohttpasio       |
|-------------------------------|------------------|--------------------|
| Routing                       | Manual           | Built-in router    |
| Middleware                    | None             | Chain system       |
| Response builder              | Manual           | Fluent API         |
| WebSocket                     | Yes (low-level)  | Yes (high-level)   |
| HTTPS/TLS                     | Yes              | Yes (OpenSSL)      |
| Crypto primitives             | No               | Yes (OpenSSL EVP)  |
| Keep-alive                    | Manual           | Automatic          |
| Windows support               | Yes              | Yes                |
| Boost dependency              | Required         | None               |
| C++ standard                  | C++11+           | C++23              |
| Learning curve                | Steep            | Express-like       |

## License

MIT License. See [LICENSE](LICENSE) for details.

### Acknowledgments

- [picohttpparser](https://github.com/h2o/picohttpparser): Kazuho Oku, Tokuhiro Matsuno, Daisuke Murase, Shigeo Mitsunari (MIT)
- [ASIO](https://github.com/chriskohlhoff/asio): Christopher M. Kohlhoff (BSL-1.0)
- [Catch2](https://github.com/catchorg/Catch2): Martin Hořeňovský et al. (BSL-1.0)
- [OpenSSL](https://www.openssl.org): OpenSSL Project (Apache-2.0)
