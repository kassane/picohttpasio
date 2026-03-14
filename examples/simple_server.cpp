// simple_server.cpp — demonstrates the picohttpasio SDK
//
// Build:
//   cmake -B build && cmake --build build
//   ./build/examples/simple_server
//
// Then:
//   curl http://localhost:8080/           → serves ./www/index.html
//   curl http://localhost:8080/hello      → route handler
//   curl http://localhost:8080/users/42   → path parameter
//   curl -r 0-99 http://localhost:8080/   → range request (HTTP 206)
//
// Static file features enabled:
//   - 30+ MIME types
//   - ETag / Last-Modified / HTTP 304 caching
//   - Range requests (HTTP 206 Partial Content)
//   - Directory listing (on by default in this demo)

#include <server.hpp>
#include <iostream>

int main() {
    try {
        asio::io_context io;

        pico::HTTPServer server(io, 8080, "./www");

        // Configure static file serving
        server.static_config().directory_listing = true;   // show dir listings
        server.static_config().caching           = true;   // ETag + 304 support
        server.static_config().range_requests    = true;   // HTTP 206 support
#ifdef PICO_ENABLE_COMPRESSION
        server.static_config().compression       = true;   // gzip if accepted
#endif
        // Add a custom MIME type
        server.static_config().extra_mime_types[".ts"] = "application/typescript";

        // Global logging middleware
        server.router().use(pico::logging_middleware());

        // Application routes (take priority over static files)
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

        std::cout << "Listening on http://localhost:8080\n"
                  << "  GET  /          → ./www/ (static files, dir listing ON)\n"
                  << "  GET  /hello     → route handler\n"
                  << "  GET  /users/:id → path parameter demo\n"
                  << "  POST /echo      → echo request body\n"
                  << "  GET  /status    → JSON status\n";
        io.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
