// techempower_bench.cpp — TechEmpower-style benchmark endpoints
//
// Implements the two simplest TechEmpower Web Framework Benchmarks categories:
//   Plaintext — GET /plaintext  → "Hello, World!"  (text/plain)
//   JSON      — GET /json       → {"message":"Hello, World!"}  (application/json)
//
// Reference: https://www.techempower.com/benchmarks/#section=code
//
// Build & run:
//   cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --parallel
//   ./build/examples/techempower_bench
//
// Benchmark (requires wrk):
//   wrk -t4 -c64 -d10s http://localhost:8080/plaintext
//   wrk -t4 -c64 -d10s http://localhost:8080/json

#include <server.hpp>
#include <iostream>

int main() {
    try {
        asio::io_context io;
        pico::HTTPServer server(io, 8080);

        server.router()
            // ----------------------------------------------------------------
            // Plaintext — category 6
            // Returns the string "Hello, World!" as text/plain; charset=utf-8.
            // ----------------------------------------------------------------
            .get("/plaintext", [](pico::Request&, pico::Response& res) {
                res = pico::Response::ok("Hello, World!");
            })

            // ----------------------------------------------------------------
            // JSON serialization — category 1
            // Returns a JSON object {"message":"Hello, World!"}.
            // ----------------------------------------------------------------
            .get("/json", [](pico::Request&, pico::Response& res) {
                res = pico::Response::make()
                    .json(R"({"message":"Hello, World!"})");
            });

        std::cout << "TechEmpower bench server listening on http://localhost:8080\n"
                  << "  GET /plaintext   — Plaintext (category 6)\n"
                  << "  GET /json        — JSON serialization (category 1)\n";
        io.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
