add_library(OrderBook_lib OrderBook.cpp OrderBook.hpp
        main.cpp)
target_include_directories(OrderBook_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})