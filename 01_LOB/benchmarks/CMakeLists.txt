if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to 'Release' as none was specified.")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)

    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

add_executable(latency_bench orderbook_bench.cpp)

target_link_libraries(latency_bench PRIVATE lob_core benchmark::benchmark)

if(APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_link_options("-L/opt/homebrew/lib" "-lstdc++")
endif()