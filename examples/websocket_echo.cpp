// websocket_echo.cpp — WebSocket echo server
//
// Connect with:
//   websocat ws://localhost:8080/ws
//   # or browser: new WebSocket("ws://localhost:8080/ws")

#include <server.hpp>
#include <iostream>

int main() {
    try {
        asio::io_context io;
        pico::HTTPServer server(io, 8080);

        // HTTP route to confirm the server is running
        server.router().get("/", [](pico::Request&, pico::Response& res) {
            res = pico::Response::make().html(
                R"(<html><body>
                   <h1>WebSocket Echo Server</h1>
                   <p>Connect via ws://localhost:8080/ws</p>
                   </body></html>)");
        });

        // WebSocket upgrade handler — registered for the /ws path
        // The HTTP upgrade is handled automatically by the server when it
        // sees "Upgrade: websocket". The on_websocket callback fires after
        // the 101 response is sent.
        server.on_websocket([](std::shared_ptr<pico::ws::WebSocketConnection> conn) {
            std::cout << "WebSocket client connected\n";

            conn->on_message([conn](pico::ws::Frame frame) {
                std::cout << "Received: " << frame.payload << '\n';
                // Echo back
                conn->send(frame.payload, frame.opcode);
            });

            conn->on_close([]{
                std::cout << "WebSocket client disconnected\n";
            });
        });

        std::cout << "WebSocket echo server on ws://localhost:8080/ws\n";
        io.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
