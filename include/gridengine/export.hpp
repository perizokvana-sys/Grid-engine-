// GridEngine - include/gridengine/export.hpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace gridengine {

enum class ExportPlatform { windows, linux };
enum class BuildType { debug, release };

struct BuildSettings {
    std::string profile{"Default"};
    ExportPlatform platform{ExportPlatform::windows};
    BuildType type{BuildType::debug};
    unsigned width{1280};
    unsigned height{720};
    bool vsync{true};
    unsigned fps_limit{60}; // 0 means uncapped
    // When true the exported package also contains a compiled native game
    // executable (bin/), built with the toolchain described below.
    bool build_executable{false};
};

// Everything the exporter needs to compile the game runtime into the package.
// Paths are resolved by the caller (the editor passes its own build layout).
struct ExportToolchain {
    std::filesystem::path cmake;                 // cmake executable
    std::filesystem::path engine_include_dir;    // contains gridengine/*.hpp
    std::filesystem::path gridengine_static_lib; // prebuilt engine library
    std::filesystem::path sdl_include_dir;       // contains SDL.h
    std::filesystem::path sdl_static_lib;        // prebuilt SDL3 static library
    std::filesystem::path game_runtime_source;   // src/tools/game_runtime.cpp
    std::string generator;                       // optional CMake -G generator ("" = default)
    // Feature macros the prebuilt engine library was compiled with. The runtime
    // must use the same set, otherwise the engine behavior compiled into the
    // executable does not match the library (e.g. audio/textures missing).
    std::string feature_macros{"GRIDENGINE_WITH_AUDIO=1;GRIDENGINE_WITH_TEXTURES=1"};
    // Default-constructs to the layout of a standard build tree next to the
    // engine source rooted at engine_root (engine root: dependencies/ + src/).
    [[nodiscard]] bool resolvable() const;
};

struct ExportResult {
    std::filesystem::path directory;
    std::string error;
    [[nodiscard]] explicit operator bool() const noexcept { return error.empty(); }
};

// Copies a project's descriptor, assets/ and scenes/ into a new package directory.
// The destination must not exist; symlinks and filesystem escapes are rejected.
// With settings.build_executable the package also receives a compiled game
// executable built for the host toolchain (Windows x64 with MSVC).
[[nodiscard]] ExportResult export_project(const std::filesystem::path& project_file,
                                          const std::filesystem::path& destination,
                                          const BuildSettings& settings = {},
                                          const ExportToolchain* toolchain = nullptr);
[[nodiscard]] std::string_view export_platform_name(ExportPlatform) noexcept;
[[nodiscard]] std::string_view build_type_name(BuildType) noexcept;

// Locates the CMake executable the exporter drives. The bundled toolchain
// (<start_dir | current dir>/dependencies/cmake/<version>/bin) wins; when it is
// absent - the repository does not store the tool's binaries - a system CMake
// from PATH is used instead. Returns an empty path when neither exists, which
// is what ExportToolchain::resolvable() reports back to the caller.
[[nodiscard]] std::filesystem::path find_cmake_executable(
    const std::filesystem::path& start_dir = {});

} // namespace gridengine
