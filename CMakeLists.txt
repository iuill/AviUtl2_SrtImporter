cmake_minimum_required(VERSION 3.20)

project(SrtImporter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# AviUtl ExEdit2 plugin is a DLL
add_library(SrtImporter SHARED SrtImporter.cpp)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    message([<STATUS>] "Release mode" ...)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -DNDEBUG")
endif()

target_include_directories(SrtImporter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/SDK)

# Win32 UI + common dialog APIs
target_link_libraries(SrtImporter PRIVATE user32 comdlg32)

# Optional: ensure wide-char APIs are the default
target_compile_definitions(SrtImporter PRIVATE UNICODE _UNICODE)

# Build the DLL with .aux2 extension to match AviUtl plugin convention
set_target_properties(SrtImporter PROPERTIES
    OUTPUT_NAME "SrtImporter"
    SUFFIX ".aux2"
    PREFIX ""
)
