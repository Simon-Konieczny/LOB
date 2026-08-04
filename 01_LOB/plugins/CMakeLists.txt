# Plugin System Configuration
# This file configures the plugin infrastructure for the LOB system.

# Create the plugin manager as a header-only library (interface library)
add_library(plugin_manager INTERFACE)

# Add include directories
target_include_directories(plugin_manager 
    INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

# Link dependencies
target_link_libraries(plugin_manager INTERFACE lob_core)
