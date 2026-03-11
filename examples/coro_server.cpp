// coro_server.cpp — CoroServer (C++20 coroutines) demo
//
// Demonstrates the coroutine-based HTTP server: same routing API as
// HTTPServer but using asio::awaitable<T> internally — no callbacks.
//
//   cmake --build build && ./build/examples/coro_server
//   curl http://localhost:8080/hello
//   curl -X POST http://localhost:8080/echo -d 'hi there'

#include <coro_server.hpp>
#include <iostream>

int main() {
    try {
        asio::io_context io;
        pico::CoroServer server(io, 8080, "./www");

        server.router()
            .use(pico::logging_middleware())
            .get("/hello", [](pico::Request&, pico::Response& res) {
                res = pico::Response::ok("Hello from coroutines!\n");
            })
            .get("/users/:id", [](pico::Request& req, pico::Response& res) {
                res = pico::Response::make()
                    .json(std::string(R"({"id":")") + std::string(req.param("id")) + R"("})");
            })
            .post("/echo", [](pico::Request& req, pico::Response& res) {
                res = pico::Response::ok(req.body);
            })
            .get("/status", [](pico::Request&, pico::Response& res) {
                res = pico::Response::make().json(R"({"status":"ok","server":"CoroServer"})");
            });

        server.run();  // spawns accept coroutine

        std::cout << "CoroServer running on http://localhost:8080\n"
                  << "  GET  /hello\n"
                  << "  GET  /users/:id\n"
                  << "  POST /echo\n"
                  << "  GET  /status\n";

        io.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
