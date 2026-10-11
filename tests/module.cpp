// Native C++ module integration test.
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.
//
// The reference sample module (samples/modules/spinner) is compiled with the
// Visual Studio toolchain exactly as documented, then loaded through the same
// ModuleLoader the game runtime uses. The test verifies the module loads, its
// behavior registers, and the behavior actually modifies the scene.
//
// Skipped (exit 0) when no Visual Studio build tools are available.

#include "gridengine/module.hpp"
#include "gridengine/scene.hpp"

#include <cmath>
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

#ifdef _WIN32
int run_and_capture(const std::string& command, std::string& output) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE read_end = nullptr;
    HANDLE write_end = nullptr;
    if (!CreatePipe(&read_end, &write_end, &sa, 0)) return -1;
    SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = write_end;
    si.hStdError = write_end;
    const auto wide = [](const std::string& text) {
        const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
        std::wstring out(static_cast<std::size_t>(size > 0 ? size - 1 : 0), L'\0');
        if (size > 1) MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), size);
        return out;
    };
    const std::wstring command_line = wide(command);
    std::vector<wchar_t> buffer(command_line.begin(), command_line.end());
    buffer.push_back(L'\0');
    PROCESS_INFORMATION pi{};
    const BOOL started = CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, TRUE,
                                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(write_end);
    if (!started) {
        CloseHandle(read_end);
        return -1;
    }
    std::string captured;
    char chunk[512];
    DWORD read_bytes = 0;
    while (ReadFile(read_end, chunk, sizeof(chunk), &read_bytes, nullptr) && read_bytes > 0)
        captured.append(chunk, read_bytes);
    CloseHandle(read_end);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    output = std::move(captured);
    return static_cast<int>(exit_code);
}
std::string quote(const std::string& text) { return "\"" + text + "\""; }
#else
int run_and_capture(const std::string&, std::string& output) { output = "not supported"; return -1; }
std::string quote(const std::string&) { return {}; }
#endif

#ifdef _WIN32
// The configuration must match the running test binary: a module linked against
// a differently configured engine library (or CRT) corrupts the process heap.
std::filesystem::path module_test_exe() {
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (size > 0 && size < buffer.size()) return std::filesystem::path(buffer.data());
        if (size == 0 || size > 32768) return {};
        buffer.resize(buffer.size() * 2);
    }
}
#else
std::filesystem::path module_test_exe() {
    const auto* self = "/proc/self/exe";
    std::error_code ec;
    auto resolved = fs::read_symlink(self, ec);
    return ec ? fs::path{} : resolved;
}
#endif

fs::path engine_root() {
    auto dir = fs::current_path();
    for (int depth = 0; depth < 8 && !dir.empty(); ++depth) {
        if (fs::exists(dir / "src" / "tools" / "game_runtime.cpp") &&
            fs::exists(dir / "CMakeLists.txt"))
            return dir;
        const auto parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    return {};
}

bool exists_file(const fs::path& path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}
} // namespace

int main() {
#ifdef _WIN32
    const auto root = engine_root();
    if (root.empty()) {
        std::cerr << "FATAL: cannot locate the engine source root from "
                  << fs::current_path() << '\n';
        return 1;
    }
    const auto exe = module_test_exe();
    const auto test_config = exe.empty() ? std::string{} : exe.parent_path().filename().string();
    const bool engine_debug = test_config.find("Debug") != std::string::npos ||
                              test_config.find("RelWithDebInfo") != std::string::npos;

    // Locate the Visual Studio developer environment through vswhere.
    const char* vswhere = "C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\\vswhere.exe";
    std::string installation_path;
    if (!exists_file(vswhere) ||
        run_and_capture(std::string("\"") + vswhere + "\" -latest -products * -requires "
                         "Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath",
                        installation_path) != 0) {
        std::cout << "gridengine_module: no Visual Studio C++ tools, skipped\n";
        return 0;
    }
    while (!installation_path.empty() &&
           (installation_path.back() == '\n' || installation_path.back() == '\r'))
        installation_path.pop_back();
    const fs::path vcvars = fs::path(installation_path) / "VC" / "Auxiliary" / "Build" / "vcvars64.bat";
    if (!exists_file(vcvars)) {
        std::cout << "gridengine_module: vcvars64.bat not found, skipped\n";
        return 0;
    }

    const auto workspace = fs::temp_directory_path() / "gridengine_module_test";
    std::error_code ec;
    fs::remove_all(workspace, ec);
    fs::create_directories(workspace, ec);
    const auto modules_dir = workspace / "modules";
    fs::create_directories(modules_dir, ec);

    const auto sample = root / "samples" / "modules" / "spinner" / "spinner.cpp";
    if (!exists_file(sample)) {
        std::cerr << "FATAL: missing the reference module source at " << sample << '\n';
        return 1;
    }
    const auto include_dir = root / "include";
    // The library that ships next to this test binary is the authoritative one:
    // it is the configuration the running binary was linked against.
    std::vector<fs::path> engine_libs;
    if (!exe.empty()) engine_libs.push_back(exe.parent_path() / "gridengine.lib");
    engine_libs.push_back(root / "build" / "Debug" / "gridengine.lib");
    engine_libs.push_back(root / "build" / "Release" / "gridengine.lib");
    engine_libs.push_back(root / "build" / "gridengine.lib");
    fs::path engine_lib;
    for (const auto& candidate : engine_libs)
        if (exists_file(candidate)) { engine_lib = candidate; break; }
    if (engine_lib.empty()) {
        std::cout << "gridengine_module: engine library not built, skipped\n";
        return 0;
    }
    const fs::path sibling_lib = exe.empty() ? fs::path{} : exe.parent_path() / "gridengine.lib";
    std::cout << "gridengine_module: exe=" << exe.string() << " sibling=" << sibling_lib.string()
              << " exists=" << (exists_file(sibling_lib) ? "yes" : "no") << " config="
              << (engine_debug ? "Debug" : "Release") << " lib=" << engine_lib.string() << '\n';

    // The module is built in the same CRT/iterator-debug configuration as the
    // engine library it links, matching the running process.
    const std::string module_dll = quote((modules_dir / "spinner.dll").string());
    const std::string cl = std::string("cl /nologo /LD /EHsc /std:c++20") +
                           (engine_debug ? " /MDd /Zi /D_DEBUG" : " /MD /O2") +
                           " /I " + quote(include_dir.string()) + " " +
                           quote(sample.string()) + " " + quote(engine_lib.string()) + " /Fe:" +
                           module_dll + " /Fo:" + quote((workspace / "spinner.obj").string());
    const std::string command = "cmd.exe /c \"call " + quote(vcvars.string()) + " && " + cl + "\"";
    std::string output;
    if (run_and_capture(command, output) != 0 || !exists_file(modules_dir / "spinner.dll")) {
        std::cerr << "FATAL: compiling the native module failed:\n" << output;
        return 1;
    }

    gridengine::ModuleLoader loader;
    const std::size_t loaded = loader.load_directory(modules_dir);
    if (loaded != 1 || loader.behavior_count() != 1) {
        std::cerr << "FATAL: loaded " << loaded << " module(s), "
                  << loader.behavior_count() << " behavior(s)\n";
        return 1;
    }

    gridengine::Scene scene;
    const auto spin = scene.create_entity("Spinner");
    const auto plain = scene.create_entity("Plain");
    if (!spin || !plain) { std::cerr << "FATAL: scene setup failed\n"; return 1; }
    scene.find(spin)->transform.rotation = 0.0;
    scene.find(plain)->transform.rotation = 0.0;

    loader.run_behaviors(scene, spin, 0.5);
    loader.run_behaviors(scene, plain, 0.5);

    const double spin_rotation = scene.find(spin)->transform.rotation;
    const double plain_rotation = scene.find(plain)->transform.rotation;
    if (std::abs(spin_rotation - 45.0) > 1e-6) {
        std::cerr << "FATAL: the native behavior did not rotate (got " << spin_rotation << ")\n";
        fs::remove_all(workspace, ec);
        return 1;
    }
    if (std::abs(plain_rotation) > 1e-6) {
        std::cerr << "FATAL: the behavior touched an entity it does not own\n";
        fs::remove_all(workspace, ec);
        return 1;
    }

    fs::remove_all(workspace, ec);
    std::cout << "gridengine_module: ok (rotation=" << spin_rotation << ")\n";
    return 0;
#else
    std::cout << "gridengine_module: not supported on this platform, skipped\n";
    return 0;
#endif
}
