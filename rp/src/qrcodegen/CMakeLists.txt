# Create a library for the settings files
add_library(qrcodegen
    qrcodegen.c
)

# Add the include directory (for qrcodegen.h)
target_include_directories(qrcodegen PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Link necessary libraries
target_link_libraries(qrcodegen
    pico_stdlib    # Core Pico SDK library
)
