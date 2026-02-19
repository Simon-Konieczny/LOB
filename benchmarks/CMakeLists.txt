add_executable(latency_bench orderbook_bench.cpp)

target_link_libraries(latency_bench PRIVATE OrderBook_lib)

if(APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_link_options("-L/opt/homebrew/lib" "-lstdc++")
endif()