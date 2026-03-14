// https_client.cpp — demonstrates HTTPSClient (async HTTPS/1.1 client)
//
// Requirements: PICO_ENABLE_TLS (OpenSSL)
//
// Build:
//   cmake -B build -DPICO_ENABLE_TLS=ON && cmake --build build
//   ./build/examples/https_client [host] [port] [path]
//
// Examples:
//   # Against a local HTTPS server (self-signed cert — verification disabled)
//   ./build/examples/https_client 127.0.0.1 8443 /hello
//
//   # Against a public API (proper cert — verification enabled)
//   ./build/examples/https_client httpbin.org 443 /get

#ifdef PICO_ENABLE_TLS

#include <client.hpp>
#include <ssl_context.hpp>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    const std::string host = (argc >= 2) ? argv[1] : "127.0.0.1";
    unsigned short    port = (argc >= 3) ? static_cast<unsigned short>(std::stoi(argv[2])) : 8443;
    const std::string path = (argc >= 4) ? argv[3] : "/";
    // Pass --no-verify as 5th arg to skip peer certificate verification
    bool verify_peer       = !(argc >= 5 && std::string(argv[4]) == "--no-verify");

    try {
        asio::io_context io;

        // Build TLS client context
        auto ctx = pico::ssl::make_client_context(verify_peer);

        pico::HTTPSClient client(io, host, port, ctx);

        std::cout << "Requesting https://" << host << ':' << port << path << '\n';

        client.get(path, [](std::optional<HTTPResponse> resp) {
            if (!resp) {
                std::cerr << "Request failed (connection error or TLS handshake failed)\n";
                return;
            }
            std::cout << "Status:  " << resp->statusCode()
                      << ' ' << resp->statusMessage() << '\n';
            std::cout << "Headers:\n";
            for (const auto& [k, v] : resp->headers())
                std::cout << "  " << k << ": " << v << '\n';
            // Note: HTTPResponse doesn't store the body internally — the body
            // was already consumed by HTTPSClient::processResponse. For a
            // complete response including body, see the HTTPClient source.
            std::cout << '\n';
        });

        io.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}

#else

#include <iostream>
int main() {
    std::cerr << "HTTPS client requires PICO_ENABLE_TLS.\n"
              << "Rebuild with: cmake -DPICO_ENABLE_TLS=ON\n";
    return 1;
}

#endif // PICO_ENABLE_TLS
