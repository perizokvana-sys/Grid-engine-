// End-to-end export verification.
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.
//
// The test packages a project with a scripted scene, builds the native Windows
// x64 game executable through the real export path (cmake + MSVC), runs the
// produced binary headless for a fixed number of frames and checks the
// simulation actually executed the Grid script.
//
// It is skipped with exit code 0 when the toolchain artifacts are not present
// (e.g. a machine without Visual Studio or without a completed engine build);
// set GRIDENGINE_SKIP_EXPORT_BUILD=1 to force the skip.

#include "gridengine/export.hpp"
#include "gridengine/project.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {
namespace fs = std::filesystem;

bool exists_file(const fs::path& path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}
bool exists_dir(const fs::path& path) {
    std::error_code ec;
    return fs::is_directory(path, ec);
}
// Walks up from the working directory until it finds the engine source root.
// The build tree also contains a "dependencies" folder (SDL), so the marker is
// the runtime source plus the top-level CMakeLists.txt.
fs::path walk_up_for_engine_root() {
    auto dir = fs::current_path();
    for (int depth = 0; depth < 8 && !dir.empty(); ++depth) {
        if (exists_file(dir / "src" / "tools" / "game_runtime.cpp") &&
            exists_file(dir / "CMakeLists.txt") && exists_dir(dir / "dependencies"))
            return dir;
        const auto parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    return {};
}
fs::path first_existing(const std::vector<fs::path>& candidates) {
    for (const auto& candidate : candidates) if (exists_file(candidate)) return candidate;
    return {};
}
fs::path bundled_cmake(const fs::path& root) {
    const auto base = root / "dependencies" / "cmake";
    std::error_code ec;
    fs::path best;
    for (const auto& entry : fs::directory_iterator(base, ec)) {
        const auto candidate = entry.path() / "bin" / "cmake.exe";
        if (exists_file(candidate) && (best.empty() || entry.path().filename() > best.filename()))
            best = candidate;
    }
    return best;
}

// Every build*/ directory of the engine root, newest first, plus the build tree
// this test binary was launched from first. Stale build folders left over from
// older configurations must not win over the tree that produced the binary.
std::vector<fs::path> build_dirs(const fs::path& root) {
    std::error_code ec;
    std::vector<fs::path> found;
    fs::path exe;
#if defined(_WIN32)
    std::wstring buffer(MAX_PATH, L'\0');
    const DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (size > 0 && size < buffer.size()) exe.assign(buffer.c_str());
#endif
    if (!exe.empty()) {
        const auto dir = exe.parent_path();
        for (int depth = 0; depth < 6; ++depth) {
            const auto parent = dir.parent_path();
            const auto name = parent.filename().string();
            if (name.rfind("build", 0) == 0) found.push_back(parent);
            if (parent == root) break;
        }
    }
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (!entry.is_directory(ec)) continue;
        const auto name = entry.path().filename().string();
        if (name.rfind("build", 0) != 0) continue;
        if (std::find(found.begin(), found.end(), entry.path()) == found.end()) found.push_back(entry.path());
    }
    return found;
}

// Finds a build artifact across every build tree, preferring the running one
// and the configuration that matches the requested build type.
std::vector<fs::path> artifact_candidates(const fs::path& root, const std::string& relative,
                                          const std::string& preferred_config) {
    std::vector<fs::path> out;
    for (const auto& build : build_dirs(root)) {
        out.push_back(build / preferred_config / relative);
        for (const char* config : {"Debug", "Release"})
            if (preferred_config != config) out.push_back(build / config / relative);
        out.push_back(build / relative);
    }
    return out;
}

void write_scripted_scene(const fs::path& scene) {
    std::ofstream out(scene, std::ios::binary);
    out << "GridScene 4\n"
        << "E 1 0 \"Player\" 0 0 0 1 1 0 0 1 \"runner.png\" 1 4294967295 1 1 1 "
        << "\"position_x += 600 * delta;\"\n";
}

// Runs the exported game with SDL's offscreen video driver for a fixed frame
// count and captures its output. std::system() is avoided because cmd.exe mangles
// quoted paths; CreateProcess keeps the invocation exact.
int run_headless(const fs::path& exe, const fs::path& project_descriptor, int frames,
                 std::string& output) {
    const auto log = exe.parent_path() / "run.log";
    fs::remove(log);
#ifdef _WIN32
    _putenv_s("SDL_VIDEODRIVER", "offscreen");
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE handle = CreateFileW(log.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return -1;
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = handle;
    si.hStdError = handle;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    const auto wide = [](const std::string& text) {
        const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
        std::wstring out(static_cast<std::size_t>(size > 0 ? size - 1 : 0), L'\0');
        if (size > 1) MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), size);
        return out;
    };
    const std::wstring cmdline = L"\"" + exe.wstring() + L"\" --project \"" +
                                 wide(project_descriptor.string()) + L"\" --frames " +
                                 std::to_wstring(frames);
    std::vector<wchar_t> buffer(cmdline.begin(), cmdline.end());
    buffer.push_back(L'\0');
    PROCESS_INFORMATION pi{};
    const BOOL started = CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(handle);
    if (!started) return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    std::ifstream in(log);
    output.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return static_cast<int>(code);
#else
    setenv("SDL_VIDEODRIVER", "offscreen", 1);
    const std::string command = "\"" + exe.string() + "\" --project \"" + project_descriptor.string() +
                                "\" --frames " + std::to_string(frames) + " > \"" + log.string() +
                                "\" 2>&1";
    const int code = std::system(command.c_str());
    std::ifstream in(log);
    output.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return code;
#endif
}
} // namespace

int main() {
    if (std::getenv("GRIDENGINE_SKIP_EXPORT_BUILD")) {
        std::cout << "gridengine_export_build: skipped by request\n";
        return 0;
    }
    const auto root = walk_up_for_engine_root();
    if (root.empty()) {
        std::cerr << "FATAL: cannot locate the engine source root from "
                  << fs::current_path() << '\n';
        return 1;
    }
    const auto toolchain = [&] {
        gridengine::ExportToolchain t;
        t.cmake = bundled_cmake(root);
        t.engine_include_dir = root / "include";
        t.gridengine_static_lib = first_existing(
            artifact_candidates(root, "gridengine.lib", "Release"));
        t.sdl_include_dir = root / "dependencies" / "sdl3-3.4.18" / "SDL3-3.4.18" / "include";
        t.sdl_static_lib = first_existing(artifact_candidates(
            root, "dependencies/sdl3-3.4.18/SDL3-3.4.18/Release/SDL3-static.lib", "Release"));
        t.game_runtime_source = root / "src" / "tools" / "game_runtime.cpp";
        return t;
    }();
    if (!toolchain.resolvable()) {
        std::cout << "gridengine_export_build: toolchain artifacts unavailable, skipped\n";
        std::cout << "  cmake=" << toolchain.cmake << "\n  engine=" << toolchain.gridengine_static_lib
                  << "\n  sdl=" << toolchain.sdl_static_lib
                  << "\n  runtime=" << toolchain.game_runtime_source << '\n';
        return 0;
    }

    const auto workspace = fs::temp_directory_path() / "gridengine_export_build_test";
    std::error_code ec;
    fs::remove_all(workspace, ec);
    // The package must live outside the exported project (the exporter rejects
    // destinations inside the source tree), so the project gets its own folder.
    const auto project_dir = workspace / "ExportTest";
    if (!fs::create_directories(project_dir / "scenes", ec) || ec) {
        std::cerr << "FATAL: cannot create the test workspace\n";
        return 1;
    }

    const auto project = gridengine::create_project(project_dir, "Export Test");
    if (!project) {
        std::cerr << "FATAL: create_project failed: " << project.error << '\n';
        return 1;
    }
    write_scripted_scene(project_dir / "scenes" / "main.scene");

    gridengine::BuildSettings settings;
    settings.build_executable = true;
    // The package build must match the engine library configuration, otherwise
    // MSVC rejects the mixed Debug/Release CRT at link time.
    settings.type = gridengine::BuildType::release;
    settings.width = 800;
    settings.height = 480;
    settings.fps_limit = 60;

    const auto package = workspace / "package";
    const auto result = gridengine::export_project(project.project.file, package, settings, &toolchain);
    if (!result) {
        std::cerr << "FATAL: export_project failed: " << result.error << '\n';
        fs::remove_all(workspace, ec);
        return 1;
    }

    const auto exe = package / "bin" / "GridEngineGame.exe";
    if (!exists_file(exe)) {
        std::cerr << "FATAL: the exported package has no game executable\n";
        fs::remove_all(workspace, ec);
        return 1;
    }
    if (!exists_file(package / "scenes" / "main.scene")) {
        std::cerr << "FATAL: the exported package has no scenes\n";
        fs::remove_all(workspace, ec);
        return 1;
    }
    std::ifstream cfg(package / "build.gridcfg");
    std::string cfg_text((std::istreambuf_iterator<char>(cfg)), std::istreambuf_iterator<char>());
    if (cfg_text.find("binary_included=true") == std::string::npos) {
        std::cerr << "FATAL: build.gridcfg does not record the binary\n";
        fs::remove_all(workspace, ec);
        return 1;
    }

    std::string output;
    const int code = run_headless(exe, project.project.file, 10, output);
    if (code != 0 || output.find("GRIDGAME_OK frames=10") == std::string::npos) {
        std::cerr << "FATAL: the exported game did not run 10 frames (exit " << code << ")\n"
                  << output;
        fs::remove_all(workspace, ec);
        return 1;
    }
    if (output.find("script error") != std::string::npos) {
        std::cerr << "FATAL: the exported game reported a script error:\n" << output;
        fs::remove_all(workspace, ec);
        return 1;
    }

    // The scene script moves the entity 600 units/s; 10 frames at 1/60s = 100.
    const auto pos_at = output.find("pos_x=");
    if (pos_at == std::string::npos) {
        std::cerr << "FATAL: the exported game did not report a position\n" << output;
        fs::remove_all(workspace, ec);
        return 1;
    }
    const double pos_x = std::stod(output.substr(pos_at + 6, 12));
    if (pos_x < 99.9 || pos_x > 100.1) {
        std::cerr << "FATAL: script did not advance the entity (pos_x=" << pos_x << ")\n" << output;
        fs::remove_all(workspace, ec);
        return 1;
    }

    fs::remove_all(workspace, ec);
    std::cout << "gridengine_export_build: ok (exe=" << exe.filename().string()
              << ", pos_x=" << pos_x << ")\n";
    return 0;
}
