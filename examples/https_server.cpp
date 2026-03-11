// https_server.cpp — HTTPS + TLS example using CoroServer
//
// Requirements: OpenSSL; generate a self-signed cert first:
//   openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem \
//           -days 3650 -nodes -subj '/CN=localhost'
//
// Build:
//   cmake -B build && cmake --build build
//   ./build/examples/https_server cert.pem key.pem
//
// Test:
//   curl -k https://localhost:8443/hello        (skip cert verify for self-signed)
//   curl -k -X POST https://localhost:8443/echo -d 'secure data'
//   # WebSocket over TLS (wss):
//   websocat --insecure wss://localhost:8443/ws

#ifdef PICO_ENABLE_TLS

#include <coro_server.hpp>
#include <ssl_context.hpp>
#include <crypto.hpp>
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <cert.pem> <key.pem> [port=8443]\n";
        return 1;
    }

    const std::string cert_file = argv[1];
    const std::string key_file  = argv[2];
    unsigned short    port      = (argc >= 4) ? static_cast<unsigned short>(std::stoi(argv[3]))
                                              : 8443;
    try {
        asio::io_context io;

        // Build TLS context
        auto tls_ctx = pico::ssl::make_server_context(cert_file, key_file);

        pico::CoroServer server(io, port);
        server.use_tls(std::move(tls_ctx));

        server.router()
            .use(pico::logging_middleware())
            .use(pico::cors_middleware())
            .get("/hello", [](pico::Request&, pico::Response& res) {
                res = pico::Response::ok("Hello over HTTPS!\n");
            })
            .post("/echo", [](pico::Request& req, pico::Response& res) {
                res = pico::Response::ok(req.body);
            })
            .get("/api/info", [](pico::Request&, pico::Response& res) {
                res = pico::Response::make().json(
                    R"({"server":"picohttpasio","tls":true,"version":"0.3"})");
            });

#ifdef PICO_ENABLE_CRYPTO
        // Example: generate a shared key and show its hex on startup
        pico::crypto::init();
        auto key = pico::crypto::secretbox::generate_key();
        std::cout << "Demo session key: " << pico::crypto::to_hex(key) << '\n';
#endif

        // WebSocket upgrade
        server.on_websocket([](std::shared_ptr<pico::ws::WebSocketConnection> conn) {
            conn->on_message([conn](pico::ws::Frame f) {
                conn->send("echo: " + f.payload);
            });
        });

        server.run();

        std::cout << "HTTPS server on https://localhost:" << port << '\n'
                  << "  GET  /hello\n"
                  << "  POST /echo\n"
                  << "  GET  /api/info\n"
                  << "  WSS  /ws  (any path)\n";

        io.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}

#else

#include <iostream>
int main() {
    std::cerr << "HTTPS support not compiled.\n"
              << "Rebuild with OpenSSL installed: apt-get install libssl-dev\n";
    return 1;
}

#endif // PICO_ENABLE_TLS
