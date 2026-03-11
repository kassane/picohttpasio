// picohttpasio — SDK demo
// Runs an HTTP server on port 8080 with a few routes.
// See examples/ for more complete usage.

#include <client.hpp>
#include <server.hpp>
#include <iostream>

int main() {
    try {
        asio::io_context io;

        // --- Server ---
        pico::HTTPServer server(io, 8080, "./www");

        server.router()
            .use(pico::logging_middleware())
            .get("/hello", [](pico::Request&, pico::Response& res) {
                res = pico::Response::ok("Hello from picohttpasio!\n");
            })
            .get("/users/:id", [](pico::Request& req, pico::Response& res) {
                res = pico::Response::make().json(
                    std::string(R"({"id":")") + std::string(req.param("id")) + R"("})");
            })
            .post("/echo", [](pico::Request& req, pico::Response& res) {
                res = pico::Response::ok(req.body);
            });

        // WebSocket support
        server.on_websocket([](std::shared_ptr<pico::ws::WebSocketConnection> conn) {
            conn->on_message([conn](pico::ws::Frame f) {
                conn->send(f.payload); // echo
            });
        });

        std::cout << "Server running on http://localhost:8080\n"
                  << "  GET /hello\n"
                  << "  GET /users/:id\n"
                  << "  POST /echo\n"
                  << "  WS  /  (any path)\n";

        io.run();
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << '\n';
        return 1;
    }
}
