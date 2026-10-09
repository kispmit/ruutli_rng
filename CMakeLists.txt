cmake_minimum_required(VERSION 3.28)
project(RyytliRNG LANGUAGES CXX)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# --- Dependencies (downloaded on first configure, same approach as the pong project) ---
include(FetchContent)

FetchContent_Declare(SFML
        GIT_REPOSITORY https://github.com/SFML/SFML.git
        GIT_TAG 3.0.2
        GIT_SHALLOW ON
        EXCLUDE_FROM_ALL
        SYSTEM)

FetchContent_Declare(json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
        EXCLUDE_FROM_ALL
        SYSTEM)

set(SFML_BUILD_AUDIO ON CACHE BOOL "" FORCE)
set(SFML_BUILD_NETWORK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SFML json)

# --- Game ---
file(GLOB_RECURSE RNG_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h)
add_executable(RyytliRNG ${RNG_SOURCES})
target_include_directories(RyytliRNG PRIVATE src)
target_compile_features(RyytliRNG PRIVATE cxx_std_20)
target_link_libraries(RyytliRNG PRIVATE SFML::Graphics SFML::Audio nlohmann_json::nlohmann_json)

# Assets are read straight from the source tree in dev builds, so editing JSON/PNGs
# does not need a rebuild. A copy is also placed next to the exe for shipping.
target_compile_definitions(RyytliRNG PRIVATE RNG_SOURCE_ASSETS="${CMAKE_SOURCE_DIR}/assets")
add_custom_command(TARGET RyytliRNG POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:RyytliRNG>/assets)

if (MINGW)
    # No need to ship libstdc++/libgcc DLLs next to the exe
    # Fully static (also libwinpthread), so the exe runs on a PC without MinGW installed
    target_link_options(RyytliRNG PRIVATE -static -static-libgcc -static-libstdc++)
endif ()

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT RyytliRNG)
