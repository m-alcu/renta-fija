cmake_minimum_required(VERSION 3.16)
project(rentafija CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# SDL3 on this system is installed in /usr/local; its exported CMake config is
# broken, so locate it through pkg-config instead.
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
add_library(SDL3::SDL3 ALIAS PkgConfig::SDL3)

# the ECB download runs on its own thread so the window stays responsive
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

set(IMGUI_DIR src/vendor/imgui)
add_executable(rentafija
    src/main.cpp
    src/model.cpp
    src/engine.cpp
    src/ecb.cpp
    src/finect.cpp
    src/updater.cpp
    src/ui.cpp
    src/tabs.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/imgui_impl_sdlrenderer3.cpp
)
target_include_directories(rentafija PRIVATE ${IMGUI_DIR} src)
target_link_libraries(rentafija PRIVATE SDL3::SDL3 Threads::Threads)

# The engine runs against synthetic curves with known answers; no SDL, no
# ImGui and no network, so these run anywhere.
enable_testing()
add_executable(engine_tests tests/engine_tests.cpp src/engine.cpp src/model.cpp src/finect.cpp
    src/ecb.cpp)
target_include_directories(engine_tests PRIVATE src)
add_test(NAME engine_tests COMMAND engine_tests)

# Downloads the series from the ECB and prints today's scoreboard and the
# backtest summary on the terminal: the app without the window. Needs the
# network, so it is not part of `ctest`.
add_executable(rf_probe tools/rf_probe.cpp src/ecb.cpp src/finect.cpp src/engine.cpp
    src/model.cpp)
target_include_directories(rf_probe PRIVATE src)
