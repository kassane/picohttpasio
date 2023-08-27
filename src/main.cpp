// #include <server.hpp>
#include <client.hpp>

int main() {
  try {
    asio::io_context io_context{4};
    
    // Server
    // HTTPServer server(io_context, 8080, "./www");
    
    // Client
    HTTPClient client(io_context, "localhost", 8080);
    // Set or modify the request string as needed
    std::string custom_request = "GET / HTTP/1.1\r\n"
                                 "Host: localhost\r\n"
                                 "User-Agent: HTTPClient\r\n"
                                 "\r\n";
    client.setRequest(custom_request);
    io_context.run();
  } catch (std::exception &e) {
    std::cerr << "Exception: " << e.what() << std::endl;
  }

  return 0;
}