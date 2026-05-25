add_executable(unit_tests
        property/OrderBook_prop.cpp
        unit/OrderBook_test.cpp
        unit/ITCHParser_test.cpp
        unit/SPSCQueue_test.cpp
        common/OrderBookHelpers.cpp
        unit/ReplayEngine_test.cpp
)

target_include_directories(unit_tests PRIVATE
        ${rapidcheck_SOURCE_DIR}/extras/gtest/include
)

target_link_libraries(unit_tests PRIVATE lob_core gtest_main rapidcheck)

include(GoogleTest)
gtest_discover_tests(unit_tests)