cmake_minimum_required(VERSION 3.20)
project(picohttp-asio VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(PICO_ENABLE_TLS "Enable HTTPS/TLS and crypto primitives via OpenSSL" ON)
option(PICO_ENABLE_COMPRESSION "Enable gzip compression for static file serving (requires zlib)" ON)

# When consumed via FetchContent/add_subdirectory, default tests and examples
# to OFF so they don't pollute the consumer's build.
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(_PICO_TOPLEVEL ON)
else()
    set(_PICO_TOPLEVEL OFF)
endif()

option(PICO_BUILD_TESTS "Build the Catch2 test suite" ${_PICO_TOPLEVEL})
option(PICO_BUILD_EXAMPLES "Build the example programs" ${_PICO_TOPLEVEL})
option(PICO_BUILD_BENCHMARKS "Build the TechEmpower benchmark server" OFF)

include(FetchContent)

# ---------------------------------------------------------------------------
# ASIO — standalone header-only, fetched directly from upstream
# ---------------------------------------------------------------------------
set(ASIO_USE_CPM OFF)
set(ASIO_CPM_FETCHCONTENT_COMPAT ON)
set(ASIO_SEPARATE_COMPILATION OFF)
set(ASIO_TAG asio-1-38-0)
set(ASIO_NO_DEPRECATED ON)

FetchContent_Declare(
    asiocmake
    GIT_REPOSITORY https://github.com/OlivierLDff/asio.cmake
    GIT_TAG main
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(asiocmake)
# ---------------------------------------------------------------------------
# Catch2 v3 (test framework — only when tests are enabled)
# ---------------------------------------------------------------------------
if(PICO_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.5.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)
endif()

# ---------------------------------------------------------------------------
# OpenSSL — TLS/HTTPS support
# ---------------------------------------------------------------------------
if(PICO_ENABLE_TLS)
    find_package(OpenSSL QUIET)
    if(NOT OpenSSL_FOUND)
        message(STATUS "OpenSSL not found on system — trying FetchContent (janbar/openssl-cmake)...")
        FetchContent_Declare(openssl_cmake
            GIT_REPOSITORY https://github.com/janbar/openssl-cmake.git
            GIT_TAG master
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(openssl_cmake)
        if(TARGET ssl AND TARGET crypto)
            set(OpenSSL_FOUND TRUE)
            # Alias to standard targets if they don't exist yet
            if(NOT TARGET OpenSSL::SSL)
                add_library(OpenSSL::SSL ALIAS ssl)
                add_library(OpenSSL::Crypto ALIAS crypto)
            endif()
        else()
            message(WARNING
                "OpenSSL build failed — HTTPS/TLS disabled.\n"
                "Install OpenSSL: apt-get install libssl-dev  (Debian/Ubuntu)\n"
                "                 dnf install openssl-devel    (Fedora/RHEL)\n"
                "                 brew install openssl         (macOS)")
            set(PICO_ENABLE_TLS OFF)
        endif()
    endif()
    if(PICO_ENABLE_TLS)
        message(STATUS "TLS enabled (OpenSSL ${OPENSSL_VERSION})")
    endif()
endif()

# ---------------------------------------------------------------------------
# zlib — optional gzip compression for static file serving
# ---------------------------------------------------------------------------
if(PICO_ENABLE_COMPRESSION)
    find_package(ZLIB QUIET)
    if(ZLIB_FOUND)
        message(STATUS "Compression enabled (zlib ${ZLIB_VERSION_STRING})")
    else()
        message(STATUS "ZLIB not found — compression disabled. "
            "Install with: apt-get install zlib1g-dev  (Debian/Ubuntu)\n"
            "                               dnf install zlib-devel  (Fedora/RHEL)\n"
            "                               brew install zlib       (macOS)")
        set(PICO_ENABLE_COMPRESSION OFF)
    endif()
endif()

# ---------------------------------------------------------------------------
# picohttpparser (C library, fetched from upstream)
# ---------------------------------------------------------------------------
FetchContent_Declare(picohttpparser
    GIT_REPOSITORY https://github.com/h2o/picohttpparser.git
    GIT_TAG master
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(picohttpparser)
if(NOT TARGET picohttpparser)
    add_library(picohttpparser STATIC ${picohttpparser_SOURCE_DIR}/picohttpparser.c)
    target_include_directories(picohttpparser PUBLIC ${picohttpparser_SOURCE_DIR})
endif()

# ---------------------------------------------------------------------------
# picohttpasio (INTERFACE / header-only SDK library)
# ---------------------------------------------------------------------------
add_library(picohttpasio INTERFACE)
target_include_directories(picohttpasio INTERFACE
    include
    ${picohttpparser_SOURCE_DIR})
add_library(picohttpparser::picohttpparser ALIAS picohttpparser)
target_link_libraries(picohttpasio INTERFACE picohttpparser asio)

if(PICO_ENABLE_TLS)
    target_link_libraries(picohttpasio INTERFACE OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(picohttpasio INTERFACE PICO_ENABLE_TLS)
endif()

if(PICO_ENABLE_COMPRESSION AND ZLIB_FOUND)
    target_link_libraries(picohttpasio INTERFACE ZLIB::ZLIB)
    target_compile_definitions(picohttpasio INTERFACE PICO_ENABLE_COMPRESSION)
endif()

if(WIN32)
    target_compile_definitions(picohttpasio INTERFACE
        _WIN32_WINNT=0x0A00
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_link_libraries(asio INTERFACE ws2_32)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(PICO_BUILD_TESTS)
    enable_testing()

    set(PICO_TEST_CERT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/certs")

    function(pico_test name)
        add_executable(${name} tests/${name}.cpp)
        target_link_libraries(${name} PRIVATE picohttpasio Catch2::Catch2WithMain)
        target_compile_definitions(${name} PRIVATE
            PICO_TEST_CERT_DIR="${PICO_TEST_CERT_DIR}")
        add_test(NAME ${name} COMMAND ${name})
    endfunction()

    pico_test(test_types)
    pico_test(test_parser)
    pico_test(test_response)
    pico_test(test_router)
    pico_test(test_websocket)
    pico_test(test_integration)
    pico_test(test_coro)
    pico_test(test_static_files)

    if(PICO_ENABLE_TLS)
        pico_test(test_crypto)
    endif()
endif()

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------
if(PICO_BUILD_EXAMPLES OR PICO_BUILD_BENCHMARKS)
    add_subdirectory(examples)
endif()
