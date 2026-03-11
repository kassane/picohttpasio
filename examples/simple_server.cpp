// simple_server.cpp — demonstrates the picohttpasio SDK
//
// Build:
//   cmake -B build && cmake --build build
//   ./build/examples/simple_server
//
// Then:
//   curl http://localhost:8080/
//   curl http://localhost:8080/hello
//   curl http://localhost:8080/users/42

#include <server.hpp>
#include <iostream>

int main() {
    try {
        asio::io_context io;

        pico::HTTPServer server(io, 8080, "./www");

        // Global logging middleware
        server.router().use(pico::logging_middleware());

        // Basic routes
        server.router()
            .get("/hello", [](pico::Request&, pico::Response& res) {
                res = pico::Response::ok("Hello, World!\n");
            })
            .get("/users/:id", [](pico::Request& req, pico::Response& res) {
                res = pico::Response::make()
                    .json(std::string(R"({"id":")") + std::string(req.param("id")) + R"("})");
            })
            .post("/echo", [](pico::Request& req, pico::Response& res) {
                res = pico::Response::ok(req.body);
            })
            .get("/status", [](pico::Request&, pico::Response& res) {
                res = pico::Response::make().json(R"({"status":"ok"})");
            });

        std::cout << "Listening on http://localhost:8080\n";
        io.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
