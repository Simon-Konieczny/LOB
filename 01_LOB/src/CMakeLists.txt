add_library(lob_core OrderBook.cpp
        Protocol.hpp
        SPSCQueue.hpp
        ITCH50.hpp
        MmapReader.hpp
        ITCHParser.hpp
        NormalizedMsg.hpp
        ReplayEngine.hpp
        IBookObserver.hpp
        OFICalculator.hpp
        OFICalculator.cpp
        SnapshotWriter.hpp
        TradeExporter.hpp)

# Include plugins directory for IAnalyticsPlugin.hpp
 target_include_directories(lob_core 
     PUBLIC
     ${CMAKE_CURRENT_SOURCE_DIR}/../plugins
 )

# main executable
add_executable(lob_main main.cpp)
target_link_libraries(lob_main PRIVATE lob_core)

# If using GCC on Mac, explicitly link the GNU C++ library to the core
if(APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_libraries(lob_core PUBLIC stdc++)
endif()

set_target_properties(lob_main PROPERTIES CLION_RC_WORKING_DIRECTORY "${PROJECT_ROOT}")

target_compile_definitions(lob_main PRIVATE "PROJECT_ROOT=\"${CMAKE_SOURCE_DIR}\"")