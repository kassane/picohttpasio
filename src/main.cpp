#include <server.hpp>

int main() {
    try {
        asio::io_context io_context;
        HTTPServer server(io_context, 8080, "./www");
        io_context.run();
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }

    return 0;
}