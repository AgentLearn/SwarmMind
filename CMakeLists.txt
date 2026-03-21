cmake_minimum_required(VERSION 3.25)
project(dronesim LANGUAGES CXX)

# ── C++23 ─────────────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # enables clangd / clang-tidy

# ── Dependencies via FetchContent ─────────────────────────────────────────────
include(FetchContent)

# nlohmann/json — header-only JSON
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)

# Asio — standalone (no Boost), header-only async I/O
FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG        asio-1-30-2
    GIT_SHALLOW    TRUE
)

# websocketpp — header-only WebSocket built on top of Asio
FetchContent_Declare(
    websocketpp
    GIT_REPOSITORY https://github.com/zaphoyd/websocketpp.git
    GIT_TAG        0.8.2
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(nlohmann_json asio websocketpp)

# glaze — reflection-based JSON serialiser, 5-10× faster than nlohmann
FetchContent_Declare(glaze
    GIT_REPOSITORY https://github.com/stephenberry/glaze.git
    GIT_TAG        v3.6.1
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(glaze)

# ── Asio include path (no CMake target exposed by that repo) ──────────────────
set(ASIO_INCLUDE_DIR "${asio_SOURCE_DIR}/asio/include")

# ── Executable ────────────────────────────────────────────────────────────────
add_executable(dronesim
    src/main.cpp
    src/sim/DecisionTree.cpp
    src/sim/Simulation.cpp
    src/server/Server.cpp
)

target_include_directories(dronesim PRIVATE
    src/
    ${ASIO_INCLUDE_DIR}
    ${websocketpp_SOURCE_DIR}
)

target_link_libraries(dronesim PRIVATE
    glaze::glaze
)

# Threads required by Asio
find_package(Threads REQUIRED)
target_link_libraries(dronesim PRIVATE Threads::Threads)

# ── Compiler flags ─────────────────────────────────────────────────────────────
target_compile_definitions(dronesim PRIVATE
    ASIO_STANDALONE          # use asio without Boost
    _WEBSOCKETPP_CPP11_STL_  # tell websocketpp to use std:: types
)

if(MSVC)
    target_compile_options(dronesim PRIVATE /W4 /WX- /utf-8)
else()
    target_compile_options(dronesim PRIVATE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter   # websocketpp headers trigger this
    )
endif()

# ── Release optimisations ─────────────────────────────────────────────────────
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(NOT MSVC)
        target_compile_options(dronesim PRIVATE -O3 -march=native)
    endif()
endif()
