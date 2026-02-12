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

# For MinGW builds, link libgcc/libstdc++ statically to avoid extra runtime DLL dependencies.
option(SRTIMPORTER_STATIC_MINGW_RUNTIME "Link MinGW runtime statically" ON)
if(MINGW AND SRTIMPORTER_STATIC_MINGW_RUNTIME)
    target_link_options(SrtImporter PRIVATE -static-libgcc -static-libstdc++)
endif()

# Output file name (without extension) can be overridden from cmake configure.
# Example: -DSRTIMPORTER_OUTPUT_NAME=SrtImporter_ex
set(SRTIMPORTER_OUTPUT_NAME "SrtImporter_ex" CACHE STRING "Output plugin filename without extension")

# Build the DLL with .aux2 extension to match AviUtl plugin convention
set_target_properties(SrtImporter PROPERTIES
    OUTPUT_NAME "${SRTIMPORTER_OUTPUT_NAME}"
    SUFFIX ".aux2"
    PREFIX ""
)
