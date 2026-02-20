# Create a library for your OrderBook logic so tests can use it too
add_library(lob_core OrderBook.cpp)

# Your main executable
add_executable(lob_main main.cpp)
target_link_libraries(lob_main PRIVATE lob_core)

# If using GCC on Mac, explicitly link the GNU C++ library to the core
if(APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_libraries(lob_core PUBLIC stdc++)
endif()