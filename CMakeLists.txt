cmake_minimum_required(VERSION 3.14)
project(picohttp-asio VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# ---------------------------------------------------------------------------
# ASIO (standalone, header-only)
# ---------------------------------------------------------------------------
find_package(Asio 1.28.1 QUIET)
if (NOT Asio_FOUND)
    FetchContent_Declare(asio
        GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
        GIT_TAG        asio-1-28-1
        GIT_SHALLOW    TRUE)
    FetchContent_GetProperties(asio)
    if (NOT asio_POPULATED)
        FetchContent_Populate(asio)
        find_package(Threads REQUIRED)
        add_library(asio INTERFACE)
        target_include_directories(asio INTERFACE ${asio_SOURCE_DIR}/asio/include)
        target_compile_definitions(asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)
        target_link_libraries(asio INTERFACE Threads::Threads)
    endif()
endif()

# ---------------------------------------------------------------------------
# Catch2 v3 (test framework)
# ---------------------------------------------------------------------------
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(Catch2)

# ---------------------------------------------------------------------------
# picohttpparser (C library, compiled once)
# ---------------------------------------------------------------------------
add_library(picohttpparser STATIC src/picohttpparser.c)
target_include_directories(picohttpparser PUBLIC include)

# ---------------------------------------------------------------------------
# picohttpasio (INTERFACE / header-only SDK library)
# ---------------------------------------------------------------------------
add_library(picohttpasio INTERFACE)
target_include_directories(picohttpasio INTERFACE include)
target_link_libraries(picohttpasio INTERFACE picohttpparser asio)

# ---------------------------------------------------------------------------
# http_demo  (main entry point — server + client demo)
# ---------------------------------------------------------------------------
add_executable(http_demo src/main.cpp)
target_link_libraries(http_demo PRIVATE picohttpasio)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()

function(pico_test name)
    add_executable(${name} tests/${name}.cpp)
    target_link_libraries(${name} PRIVATE picohttpasio Catch2::Catch2WithMain)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

pico_test(test_types)
pico_test(test_parser)
pico_test(test_response)
pico_test(test_router)
pico_test(test_websocket)
pico_test(test_integration)

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------
add_subdirectory(examples)
