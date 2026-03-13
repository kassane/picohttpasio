function(pico_example name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE picohttpasio)
endfunction()

if(PICO_BUILD_EXAMPLES)
    pico_example(simple_server)
    pico_example(rest_api)
    pico_example(websocket_echo)

    if(PICO_ENABLE_TLS)
        pico_example(coro_server)
        pico_example(https_server)
    endif()
endif()

if(PICO_BUILD_BENCHMARKS)
    pico_example(techempower_bench)
endif()
