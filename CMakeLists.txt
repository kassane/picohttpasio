cmake_minimum_required(VERSION 3.14)
project(picohttp-asio)

set(CMAKE_CXX_STANDARD 11)
include(FetchContent)

find_package(Asio 1.28.1 QUIET)
if (NOT Asio_FOUND)
    FetchContent_Declare(asio GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
        GIT_TAG asio-1-28-1)
    FetchContent_GetProperties(asio)
    if (NOT asio_POPULATED)
        FetchContent_Populate(asio)
        find_package(Threads REQUIRED)
        add_library(asio INTERFACE)
        target_include_directories(asio INTERFACE ${asio_SOURCE_DIR}/asio/include)
        target_compile_definitions(asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)
        target_link_libraries(asio INTERFACE Threads::Threads)
    endif ()
endif()

add_library(picohttpparser src/picohttpparser.c)
add_executable(http_client src/main.cpp)

include_directories("include")
# Link the picohttpparser and asio libraries
target_link_libraries(http_client 
    PUBLIC picohttpparser
    PRIVATE asio
)
