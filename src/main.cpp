// GridEngine - src/main.cpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#include "gridengine/engine.hpp"
#include "gridengine/export.hpp"
#include "gridengine/project.hpp"
#include "gridengine/tilemap.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {
void help() {
    std::cout << "GridEngine Project Manager (v" << gridengine::version() << ")\nUsage:\n"
                 "  gridengine_demo --new <directory> <project-name>\n"
                 "  gridengine_demo --open <path-to-project.grid>\n"
                 "  gridengine_demo --export <path-to-project.grid> <destination> [--release] [--build]\n";
}

void show(const gridengine::ProjectInfo& project, const gridengine::TileMap2D& map) {
    std::cout << "\n  GRIDENGINE | GridEditor | " << project.name
                 << "\n  ┌──────────────────┬──────────────────────────────┬────────────────┐"
                 "\n  │ HIERARCHY        │ TILEMAP                      │ INSPECTOR      │"
                 "\n  ├──────────────────┼──────────────────────────────┼────────────────┤"
                 "\n  │ Scene            │";
    for (std::uint32_t y = 0; y < map.height(); ++y) {
        std::cout << "\n  │" << (y == 0 ? " TileMap" : "") << "               │ ";
        for (std::uint32_t x = 0; x < map.width(); ++x) {
            const auto cell = map.cell(x, y);
            const auto id = cell ? cell->tile_id : 0;
            std::cout << (id == 0 ? '.' : id == 2 ? 'P' : '#');
        }
        std::cout << "                 │"
                  << (y == 0 ? " Grid: " + std::to_string(map.tile_width()) + "x" +
                                      std::to_string(map.tile_height()) : "");
    }
    std::cout << "\n  │ + Entity        │ [Select] [Paint] [Erase]    │ Tile palette   │"
                 "\n  ├──────────────────┴──────────────────────────────┴────────────────┤"
                 "\n  │ ASSETS  scenes/ assets/    [Play] [Stop] [Save]                    │"
                 "\n  └───────────────────────────────────────────────────────────────────┘"
                 "\n  Text prototype; the graphical SDL/OpenGL frontend is gridengine_editor.\n\n";
}

bool exists_file(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

// The build directory the running executable belongs to (build, build_vs,
// build_release_v1, ...). Resolving artifacts there keeps the export toolchain
// in sync with the binary that is actually running, instead of picking an
// arbitrary build*/ entry of the working directory.
std::filesystem::path current_build_dir() {
    std::error_code ec;
    std::filesystem::path exe;
#if defined(_WIN32)
    std::wstring buffer(MAX_PATH, L'\0');
    const DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (size > 0 && size < buffer.size()) exe.assign(buffer.c_str());
#else
    exe = std::filesystem::read_symlink("/proc/self/exe", ec);
#endif
    if (ec || exe.empty()) return {};
    auto dir = exe.parent_path();
    for (int depth = 0; depth < 6 && !dir.empty(); ++depth) {
        const auto name = dir.filename().string();
        if (name.rfind("build", 0) == 0) return dir;
        const auto parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    return {};
}

// Expands a `{build}` spec against `base`, plus the literal path otherwise.
std::vector<std::filesystem::path> candidates(const std::filesystem::path& base, const std::string& spec) {
    std::vector<std::filesystem::path> out;
    if (spec.find("{build}") == std::string::npos) {
        out.push_back(base / spec);
        return out;
    }
    std::string rest = spec.substr(spec.find("{build}") + std::string("{build}").size());
    while (!rest.empty() && (rest.front() == '/' || rest.front() == '\\')) rest.erase(rest.begin());
    std::error_code iter_ec;
    if (std::filesystem::is_directory(base, iter_ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(base, iter_ec)) {
            const auto& name = entry.path().filename().string();
            if (name.rfind("build", 0) != 0 || !entry.is_directory(iter_ec)) continue;
            out.push_back(entry.path() / rest);
        }
    }
    return out;
}

// Walks up from the working directory and returns the first match of a
// repo-relative path, so the tool works from the engine root or any child.
// `{build}` expands to every build directory (build, build_vs, build_release_v1, ...).
std::filesystem::path find_up(const std::string& relative) {
    std::error_code ec;
    auto dir = std::filesystem::current_path(ec);
    if (ec) return {};
    std::vector<std::filesystem::path> starts{dir};
    // Also search from the running executable, so resolving artifacts works
    // when the tool is launched from an unrelated working directory.
    std::filesystem::path exe;
#if defined(_WIN32)
    std::wstring buffer(MAX_PATH, L'\0');
    const DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (size > 0 && size < buffer.size()) exe.assign(buffer.c_str());
#else
    exe = std::filesystem::read_symlink("/proc/self/exe", ec);
#endif
    if (!ec && !exe.empty()) starts.push_back(exe.parent_path());

    for (const auto& start : starts) {
        auto cursor = start;
        for (int depth = 0; depth < 8 && !cursor.empty(); ++depth) {
            for (const auto& candidate : candidates(cursor, relative)) {
                if (exists_file(candidate) || std::filesystem::is_directory(candidate, ec)) return candidate;
            }
            const auto parent = cursor.parent_path();
            if (parent == cursor) break;
            cursor = parent;
        }
    }
    return {};
}

std::filesystem::path find_bundled_cmake() {
    const auto base = find_up("dependencies/cmake");
    if (base.empty()) return {};
    std::error_code ec;
    std::filesystem::path best;
    for (const auto& entry : std::filesystem::directory_iterator(base, ec)) {
        const auto candidate = entry.path() / "bin" / "cmake.exe";
        if (exists_file(candidate) && (best.empty() || entry.path().filename() > best.filename()))
            best = candidate.parent_path().parent_path() / "bin" / "cmake.exe";
    }
    return best;
}

// Locates the prebuilt static libraries in the build tree, preferring the
// configuration that matches the requested build type.
std::filesystem::path find_build_artifact(const std::string& relative, const std::string& config) {
    // The build tree of the running binary wins over a working-directory scan,
    // which could match a stale build*/ folder from an older configuration.
    const auto here = current_build_dir();
    if (!here.empty()) {
        for (const auto& candidate : {here / config / relative, here / relative}) {
            if (exists_file(candidate)) return candidate;
        }
    }
    const std::vector<std::string> order{config, "Debug", "Release"};
    for (const auto& cfg : order) {
        const auto found = find_up("{build}/" + cfg + "/" + relative);
        if (!found.empty()) return found;
    }
    return find_up("{build}/" + relative);
}

gridengine::ExportToolchain resolve_toolchain(gridengine::BuildType type) {
    gridengine::ExportToolchain t;
    t.cmake = find_bundled_cmake();
    t.engine_include_dir = find_up("include");
    t.gridengine_static_lib = find_build_artifact("gridengine.lib",
        type == gridengine::BuildType::release ? "Release" : "Debug");
    t.sdl_include_dir = find_up("dependencies/sdl3-3.4.18/SDL3-3.4.18/include");
    t.sdl_static_lib = find_build_artifact("dependencies/sdl3-3.4.18/SDL3-3.4.18/" +
        std::string(type == gridengine::BuildType::release ? "Release" : "Debug") +
        "/SDL3-static.lib", type == gridengine::BuildType::release ? "Release" : "Debug");
    t.game_runtime_source = find_up("src/tools/game_runtime.cpp");
    return t;
}} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { help(); return 0; }
    const std::string command = argv[1];
    if (command == "--help" || command == "-h") { help(); return 0; }

    if (command == "--export" && argc >= 4) {
        gridengine::BuildSettings settings;
        for (int i = 4; i < argc; ++i) {
            const std::string flag = argv[i];
            if (flag == "--release") settings.type = gridengine::BuildType::release;
            else if (flag == "--debug") settings.type = gridengine::BuildType::debug;
            else if (flag == "--build") settings.build_executable = true;
            else { help(); return 2; }
        }
        gridengine::ExportToolchain toolchain;
        if (settings.build_executable) {
            toolchain = resolve_toolchain(settings.type);
            if (!toolchain.resolvable()) {
                std::cerr << "Export toolchain not found (built " << (settings.type == gridengine::BuildType::release ? "Release" : "Debug")
                          << " first):\n  cmake=" << toolchain.cmake
                          << "\n  engine_lib=" << toolchain.gridengine_static_lib
                          << "\n  sdl_lib=" << toolchain.sdl_static_lib
                          << "\n  include=" << toolchain.engine_include_dir
                          << "\n  runtime=" << toolchain.game_runtime_source << '\n';
                return 3;
            }
        }
        const auto result = gridengine::export_project(argv[2], argv[3], settings,
                                                       settings.build_executable ? &toolchain : nullptr);
        if (!result) { std::cerr << "Export error: " << result.error << '\n'; return 1; }
        std::cout << "Exported to " << result.directory.string() << '\n';
        if (settings.build_executable)
            std::cout << "Game executable: " << (result.directory / "bin" / "GridEngineGame.exe").string() << '\n';
        return 0;
    }

    gridengine::ProjectResult result;
    if (command == "--new" && argc == 4)
        result = gridengine::create_project(argv[2], argv[3]);
    else if (command == "--open" && argc == 3)
        result = gridengine::open_project(argv[2]);
    else { help(); return 2; }

    if (!result) { std::cerr << "Project error: " << result.error << '\n'; return 1; }
    gridengine::Engine engine({result.project.name, 60});
    if (!engine.initialize()) { std::cerr << "Editor initialization failed\n"; return 1; }

    gridengine::TileMap2D map(16, 8, 32, 32);
    for (std::uint32_t x = 0; x < map.width(); ++x) {
        (void)map.set_cell(x, 6, {1});
        if (x % 3 == 0) (void)map.set_cell(x, 5, {1});
    }
    (void)map.set_cell(3, 5, {2});
    show(result.project, map);
    std::cout << "Project: " << result.project.name << "\nDescriptor: "
              << result.project.file.string() << "\nTileMap ready: " << map.width() << 'x'
              << map.height() << " cells, " << map.tile_count() << " occupied\n";
    engine.shutdown();
    return 0;
}
