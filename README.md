# HTTP Server with picohttpparser and Asio

This is a simple HTTP server implemented in C++ using the `picohttpparser` library for HTTP parsing and the Asio library for network communication. The server can serve static files and return a "Hello World Server!!" response for specific routes.

## Prerequisites

Before running this HTTP server, make sure you have the following prerequisites installed:

- C++ compiler (e.g., g++)
- CMake (for building the project) - **TODO**
- Asio library
- picohttpparser library

## Building and Running

ollow these steps to build and run the server:

1. Clone the repository:

```sh
git clone <repository-url>
cd picoasio
zig build run -Doptimize=ReleaseFast
# or
cmake -B build -DCMAKE_TYPE_BUILD=Release && cmake --build build --parallel
```
By default, the server listens on port 8080 and serves files from the "./www" directory. You can modify the root directory and other server options in the source code.
Usage Serving Static Files

To serve static files, simply place your files in the "./www" directory. The server will serve "index.html" by default when accessing the root ("/") URL.
Custom Routes

You can create custom routes by modifying the handleRequest method in the HTTPRequestHandler class. For example, to respond with a static "Hello World Server!!" response for the "/hello" route, you can add:

# License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.

# Acknowledgments

    The picohttpparser library: [GitHub Repository](https://github.com/h2o/picohttpparser)
    The Asio library: Asio C++ Library

Feel free to customize and extend this server to suit your needs.
