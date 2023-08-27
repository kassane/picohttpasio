#ifndef server_hpp
#define server_hpp
#include <asio.hpp>
#include <fstream>
#include <iostream>
#include <picohttpwrapper.hpp>
#include <string>

using asio::ip::tcp;

class HTTPRequestHandler {
public:
  HTTPRequestHandler(const std::string &root_dir) : rootDirectory_(root_dir) {}

  void handleRequest(const std::string &request_str, std::string &response) {
    HTTPRequest request(request_str);

    if (request.parse()) {
      if (request.getMethod() == "GET") {
        std::string path = request.getPath();
        if (path == "/") {
          serveFile("index.html", response);
        } else if (path == "/hello") {
          response = "HTTP/1.1 200 OK\r\n"
                     "Content-Length: 13\r\n"
                     "\r\n"
                     "Hello, C++ Server!!\n";
        } else {
          response = "HTTP/1.1 404 Not Found\r\n"
                     "Content-Length: 12\r\n"
                     "\r\n"
                     "File not found";
        }
      } else {
        response = "HTTP/1.1 405 Method Not Allowed\r\n"
                   "Content-Length: 20\r\n"
                   "\r\n"
                   "Method Not Allowed";
      }
    } else {
      // Invalid request.
      response = "HTTP/1.1 400 Bad Request\r\n"
                 "Content-Length: 12\r\n"
                 "\r\n"
                 "Bad Request";
    }
  }

private:
  void sendErrorResponse(const std::string &error_msg, std::string &response) {
    response = "HTTP/1.1 " + error_msg +
               "\r\n"
               "Content-Length: " +
               std::to_string(error_msg.size()) +
               "\r\n"
               "\r\n" +
               error_msg;
  }
  void serveFile(const std::string &filename, std::string &response) {
    std::string fullFilePath = rootDirectory_ + "/" + filename;
    std::ifstream file(fullFilePath, std::ios::binary);
    if (file) {
      std::string fileContents((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
      response = "HTTP/1.1 200 OK\r\n"
                 "Content-Length: " +
                 std::to_string(fileContents.size()) +
                 "\r\n"
                 "\r\n" +
                 fileContents;
    } else {
      response = "HTTP/1.1 404 Not Found\r\n"
                 "Content-Length: 12\r\n"
                 "\r\n"
                 "File not found";
    }
    file.close();
  }
  std::string rootDirectory_{};
};

class HTTPServerConnection
    : public std::enable_shared_from_this<HTTPServerConnection> {
public:
  HTTPServerConnection(tcp::socket socket, HTTPRequestHandler &requestHandler)
      : socket_(std::move(socket)), requestHandler_(requestHandler) {}

  void start() { readRequest(); }

private:
  void readRequest() {
    auto self(shared_from_this());
    asio::async_read_until(
        socket_, asio::dynamic_buffer(request_data_), "\r\n\r\n",
        [this, self](std::error_code ec, std::size_t bytes_transferred) {
          if (!ec) {
            std::string request_str =
                request_data_.substr(0, bytes_transferred);
            std::string response;
            requestHandler_.handleRequest(request_str, response);
            sendResponse(response);
          }
        });
  }

  void sendResponse(const std::string &response) {
    auto self(shared_from_this());
    asio::async_write(
        socket_, asio::buffer(response),
        [this, self](std::error_code ec, std::size_t /*bytes_transferred*/) {
          if (!ec) {
            // Connection will be closed after the response is sent.
            std::error_code ignored_ec;
            socket_.shutdown(tcp::socket::shutdown_both, ignored_ec);
          }
        });
  }

  tcp::socket socket_;
  std::string request_data_;
  HTTPRequestHandler &requestHandler_;
};

class HTTPServer {
public:
  HTTPServer(asio::io_context &io_context, short port,
             const std::string &root_dir)
      : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)),
        requestHandler_(root_dir) {
    startAccept();
  }

private:
  void startAccept() {
    acceptor_.async_accept([this](std::error_code ec, tcp::socket socket) {
      if (!ec) {
        std::make_shared<HTTPServerConnection>(std::move(socket),
                                               requestHandler_)
            ->start();
      }
      startAccept();
    });
  }

  tcp::acceptor acceptor_;
  HTTPRequestHandler requestHandler_;
};
#endif // server_hpp