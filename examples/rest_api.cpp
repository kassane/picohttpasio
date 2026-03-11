// rest_api.cpp — demonstrates a CRUD-style REST API with middleware
//
// Endpoints:
//   GET    /api/items           → list items
//   GET    /api/items/:id       → get item by id
//   POST   /api/items           → create item (body = item name)
//   DELETE /api/items/:id       → delete item

#include <server.hpp>
#include <iostream>
#include <map>
#include <mutex>
#include <string>

int main() {
    try {
        asio::io_context io;
        pico::HTTPServer server(io, 8080);

        // In-memory store
        std::map<int, std::string> store = {{1, "apple"}, {2, "banana"}};
        int next_id = 3;
        std::mutex mtx;

        // Logging + CORS middleware
        server.router()
            .use(pico::logging_middleware())
            .use(pico::cors_middleware());

        // GET /api/items
        server.router().get("/api/items", [&](pico::Request&, pico::Response& res) {
            std::lock_guard lock(mtx);
            std::string json = "[";
            bool first = true;
            for (const auto& [id, name] : store) {
                if (!first) json += ',';
                json += R"({"id":)" + std::to_string(id) + R"(,"name":")" + name + R"("})";
                first = false;
            }
            json += "]";
            res = pico::Response::make().json(json);
        });

        // GET /api/items/:id
        server.router().get("/api/items/:id", [&](pico::Request& req, pico::Response& res) {
            std::lock_guard lock(mtx);
            int id = std::stoi(std::string(req.param("id")));
            auto it = store.find(id);
            if (it == store.end()) {
                res = pico::Response::not_found(R"({"error":"not found"})");
                return;
            }
            res = pico::Response::make().json(
                R"({"id":)" + std::to_string(id) + R"(,"name":")" + it->second + R"("})");
        });

        // POST /api/items
        server.router().post("/api/items", [&](pico::Request& req, pico::Response& res) {
            if (req.body.empty()) {
                res = pico::Response::bad_request(R"({"error":"body required"})");
                return;
            }
            std::lock_guard lock(mtx);
            int id = next_id++;
            store[id] = req.body;
            res = pico::Response::make(pico::StatusCode::Created)
                .json(R"({"id":)" + std::to_string(id) + R"(,"name":")" + req.body + R"("})");
        });

        // DELETE /api/items/:id
        server.router().del("/api/items/:id", [&](pico::Request& req, pico::Response& res) {
            std::lock_guard lock(mtx);
            int id = std::stoi(std::string(req.param("id")));
            if (store.erase(id) == 0) {
                res = pico::Response::not_found(R"({"error":"not found"})");
                return;
            }
            res = pico::Response::no_content();
        });

        std::cout << "REST API listening on http://localhost:8080\n";
        std::cout << "  GET    /api/items\n";
        std::cout << "  GET    /api/items/:id\n";
        std::cout << "  POST   /api/items  (body=item name)\n";
        std::cout << "  DELETE /api/items/:id\n";
        io.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
