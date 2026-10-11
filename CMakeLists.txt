cmake_minimum_required(VERSION 3.20)
project(GridEngine VERSION 1.0.0 LANGUAGES CXX)

option(GRIDENGINE_BUILD_TESTS "Build GridEngine tests" ON)
option(GRIDENGINE_BUILD_EDITOR "Build the SDL/OpenGL editor frontend" ON)
option(GRIDENGINE_BUILD_AUDIO "Build the SDL3 real audio backend" ON)
option(GRIDENGINE_BUILD_TEXTURES "Build texture decoding and the OpenGL texture cache" ON)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(gridengine
    src/engine.cpp
    src/project.cpp
    src/scene.cpp
    src/editor.cpp
    src/node.cpp
    src/node_types.cpp
    src/tilemap.cpp
    src/audio.cpp
    src/particles.cpp
    src/grid.cpp
    src/export.cpp
    src/module.cpp
    src/csharp.cpp
)
if(GRIDENGINE_BUILD_AUDIO)
    # SDL3 is only needed from this point on; the audio backend links it into
    # the engine library, and the editor/game frontends link it transitively.
    target_sources(gridengine PRIVATE src/audio_device.cpp)
    target_link_libraries(gridengine PUBLIC SDL3::SDL3-static)
    target_compile_definitions(gridengine PUBLIC GRIDENGINE_WITH_AUDIO=1)
endif()
if(GRIDENGINE_BUILD_TEXTURES)
    target_sources(gridengine PRIVATE src/texture.cpp src/texture_gpu.cpp)
    if(WIN32)
        # WIC decoder (PNG/JPEG/GIF/...) and OpenGL texture upload.
        target_link_libraries(gridengine PUBLIC windowscodecs opengl32)
    endif()
    target_compile_definitions(gridengine PUBLIC GRIDENGINE_WITH_TEXTURES=1)
endif()
target_include_directories(gridengine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(gridengine PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(gridengine PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(gridengine PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(gridengine_demo src/main.cpp)
target_link_libraries(gridengine_demo PRIVATE gridengine)

# --- SDL3 (bundled in dependencies/) -----------------------------------------
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_VULKAN OFF CACHE BOOL "" FORCE)
set(SDL_DIRECTX ON CACHE BOOL "" FORCE)
set(SDL_WASAPI ON CACHE BOOL "" FORCE)
set(SDL_XINPUT ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/dependencies/sdl3-3.4.18/SDL3-3.4.18)

# --- Dear ImGui (bundled in dependencies/) -----------------------------------
add_library(imgui STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui/imgui-master/imgui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui/imgui-master/imgui_draw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui/imgui-master/imgui_tables.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui/imgui-master/imgui_widgets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui/imgui-master/backends/imgui_impl_sdl3.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui/imgui-master/backends/imgui_impl_opengl2.cpp
)
target_include_directories(imgui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui/imgui-master
    ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui/imgui-master/backends
)
target_link_libraries(imgui PUBLIC SDL3::SDL3-static)
target_compile_features(imgui PRIVATE cxx_std_20)

if(GRIDENGINE_BUILD_EDITOR)
    # Single frontend executable: the main menu and the project editor are
    # compiled into one binary; opening a project hands over in-process
    # (see editor_main in src/tools/editor_frontend.cpp).
    add_executable(gridengine_editor src/main_menu.cpp src/tools/editor_frontend.cpp)
    target_link_libraries(gridengine_editor PRIVATE gridengine imgui SDL3::SDL3-static)
    if(WIN32)
        target_link_libraries(gridengine_editor PRIVATE opengl32)
    endif()
endif()

if(GRIDENGINE_BUILD_TESTS)
    enable_testing()
    add_executable(gridengine_tests tests/smoke.cpp)
    target_link_libraries(gridengine_tests PRIVATE gridengine)
    add_test(NAME gridengine_smoke COMMAND gridengine_tests)
    add_executable(gridengine_audio_tests tests/audio.cpp)
    target_link_libraries(gridengine_audio_tests PRIVATE gridengine)
    add_test(NAME gridengine_audio COMMAND gridengine_audio_tests)
    add_executable(gridengine_particles_tests tests/particles.cpp)
    target_link_libraries(gridengine_particles_tests PRIVATE gridengine)
    add_test(NAME gridengine_particles COMMAND gridengine_particles_tests)
    add_executable(gridengine_grid_tests tests/grid.cpp)
    target_link_libraries(gridengine_grid_tests PRIVATE gridengine)
    add_test(NAME gridengine_grid COMMAND gridengine_grid_tests)
    add_executable(gridengine_export_tests tests/export.cpp)
    target_link_libraries(gridengine_export_tests PRIVATE gridengine)
    add_test(NAME gridengine_export COMMAND gridengine_export_tests)
    add_executable(gridengine_tilemap_tests tests/tilemap.cpp)
    target_link_libraries(gridengine_tilemap_tests PRIVATE gridengine)
    add_test(NAME gridengine_tilemap COMMAND gridengine_tilemap_tests)
    add_executable(gridengine_node_tests tests/node.cpp)
    target_link_libraries(gridengine_node_tests PRIVATE gridengine)
    add_test(NAME gridengine_node COMMAND gridengine_node_tests)
    # End-to-end Windows x64 export: builds a native game executable with the
    # bundled toolchain and runs it headless against a scripted scene.
    add_executable(gridengine_export_build_tests tests/export_build.cpp)
    target_link_libraries(gridengine_export_build_tests PRIVATE gridengine)
    add_test(NAME gridengine_export_build COMMAND gridengine_export_build_tests)
    # Native C++ module (samples/modules/spinner built with the local MSVC).
    add_executable(gridengine_module_tests tests/module.cpp)
    target_link_libraries(gridengine_module_tests PRIVATE gridengine)
    add_test(NAME gridengine_module COMMAND gridengine_module_tests)
    # C# behavior bridge (generated project built and run with dotnet).
    add_executable(gridengine_csharp_tests tests/csharp.cpp)
    target_link_libraries(gridengine_csharp_tests PRIVATE gridengine)
    add_test(NAME gridengine_csharp COMMAND gridengine_csharp_tests)
    # Texture decoding: real PNG (platform decoder) + BMP (built-in) + failures.
    add_executable(gridengine_texture_tests tests/texture.cpp)
    target_link_libraries(gridengine_texture_tests PRIVATE gridengine)
    add_test(NAME gridengine_texture COMMAND gridengine_texture_tests)
    # Real audio: WAV decode, SDL streams, gain, pause/resume, loop, release.
    add_executable(gridengine_audio_device_tests tests/audio_device.cpp)
    target_link_libraries(gridengine_audio_device_tests PRIVATE gridengine)
    add_test(NAME gridengine_audio_device COMMAND gridengine_audio_device_tests)
    # GridScene 6: audio sources round-trip through save/load, legacy formats stay readable.
    add_executable(gridengine_scene_audio_tests tests/scene_audio.cpp)
    target_link_libraries(gridengine_scene_audio_tests PRIVATE gridengine)
    add_test(NAME gridengine_scene_audio COMMAND gridengine_scene_audio_tests)
    # Editor state the frontend drives: hierarchy, components, play/stop, console.
    add_executable(gridengine_editor_tests tests/editor.cpp)
    target_link_libraries(gridengine_editor_tests PRIVATE gridengine)
    add_test(NAME gridengine_editor COMMAND gridengine_editor_tests)
    # Node catalog: every node type of the reference, placements and node creation.
    add_executable(gridengine_node_types_tests tests/node_types.cpp)
    target_link_libraries(gridengine_node_types_tests PRIVATE gridengine)
    add_test(NAME gridengine_node_types COMMAND gridengine_node_types_tests)
    # GridScene 7: frameset slicing, clip playback and the save/load round trip.
    add_executable(gridengine_scene_frameset_tests tests/scene_frameset.cpp)
    target_link_libraries(gridengine_scene_frameset_tests PRIVATE gridengine)
    add_test(NAME gridengine_scene_frameset COMMAND gridengine_scene_frameset_tests)
endif()

# In-tree game runtime: the same source the exporter compiles into packages.
add_executable(gridengine_game src/tools/game_runtime.cpp)
target_link_libraries(gridengine_game PRIVATE gridengine SDL3::SDL3-static)
if(WIN32)
    target_link_libraries(gridengine_game PRIVATE opengl32 imm32 winmm ole32 oleaut32 version setupapi shell32)
endif()
list(APPEND GRID_SOURCES src/texture.cpp src/texture_gpu.cpp)
