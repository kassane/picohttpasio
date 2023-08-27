#ifndef client_hpp
#define client_hpp

#include "picohttpwrapper.hpp"
#include <asio.hpp>
#include <iostream>
#include <string>

using asio::ip::tcp;

class HTTPClient {
public:
  HTTPClient(asio::io_context &io_context, const std::string &server_ip,
             unsigned short port)
      : resolver_(io_context), socket_(io_context), response_data_(),
        request_() {
    resolver_.async_resolve(
        server_ip, std::to_string(port),
        [this](std::error_code ec, tcp::resolver::results_type endpoints) {
          if (!ec) {
            asio::async_connect(socket_, endpoints,
                                [this](std::error_code ec, tcp::endpoint) {
                                  if (!ec) {
                                    sendRequest();
                                  }
                                });
          }
        });
  }

  // Method to set or modify the request string
  void setRequest(const std::string &request) { request_ = request; }

private:
  void sendRequest() {
    asio::async_write(
        socket_, asio::buffer(request_),
        [this](std::error_code ec, std::size_t /*bytes_transferred*/) {
          if (!ec) {
            readResponse();
          }
        });
  }

  void readResponse() {
    asio::async_read_until(
        socket_, response_data_, "\r\n\r\n",
        [this](std::error_code ec, std::size_t /* bytes_transferred */) {
          if (!ec) {
            std::string response_str;
            response_str.resize(response_data_.size());
            response_data_.sgetn(&response_str[0], static_cast<std::streamsize>(
                                                       response_str.size()));

            HTTPRequest response_parser(response_str);
            if (response_parser.parse()) {
              std::cout << "HTTP Status Code: "
                        << response_parser.getMinorVersion() << " "
                        << response_parser.getPath() << " "
                        << response_parser.getMinorVersion() << std::endl;

              std::cout << "Response Headers:" << std::endl;
              for (const auto &header : response_parser.getHeaders()) {
                std::cout << header.first << ": " << header.second << std::endl;
              }

              int content_length = -1;
              try {
                content_length =
                    std::stoi(response_parser.getHeader("Content-Length"));
              } catch (std::exception &e) {
                std::cerr << "Error parsing Content-Length header: " << e.what()
                          << std::endl;
              }

              if (content_length > 0) {
                // Read and print the response content
                asio::async_read(
                    socket_, response_data_,
                    [this, content_length](std::error_code ec,
                                           std::size_t /*bytes_transferred*/) {
                      if (!ec) {
                        std::string content;
                        // content.resize(bytes_transferred);
                        // response_data_.sgetn(&content[0],
                        // static_cast<std::streamsize>(bytes_transferred));

                        if (content.size() ==
                            static_cast<size_t>(content_length)) {
                          std::cout << "\nResponse Content:\n"
                                    << content << std::endl;
                        } else {
                          std::cerr << "Content length mismatch." << std::endl;
                        }
                      } else {
                        std::cerr << "Error reading response content: "
                                  << ec.message() << std::endl;
                      }
                    });
              } else {
                std::cerr << "Invalid Content-Length header." << std::endl;
              }
            } else {
              // Print the raw response for debugging
              std::cerr << "Failed to parse HTTP response." << std::endl;
              std::cerr << "Raw Response:\n" << response_str << std::endl;
            }
          } else {
            std::cerr << "Error reading response headers: " << ec.message()
                      << std::endl;
          }
        });
  }

  tcp::resolver resolver_;
  tcp::socket socket_;
  asio::streambuf response_data_;
  std::string request_; // Editable request string
};
#endif /* client_hpp */
