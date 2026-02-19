cmake_minimum_required(VERSION 4.0)
project(LOB LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-O3 -march=native)
if(APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    link_directories("/opt/homebrew/lib")
#    add_link_options("-L/opt/homebrew/lib" "-lstdc++")
#    add_link_options("-static-libstdc++" "-static-libgcc")
#    link_libraries(stdc++)
endif()

include(FetchContent)

FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/heads/main.zip
)

FetchContent_MakeAvailable(googletest)

enable_testing()
add_subdirectory(src)
add_subdirectory(tests)
add_subdirectory(benchmarks)