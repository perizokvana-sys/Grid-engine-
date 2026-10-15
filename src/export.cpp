// GridEngine - src/export.cpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#include "gridengine/export.hpp"
#include "gridengine/project.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace gridengine {
namespace fs = std::filesystem;
namespace {
bool safe_tree(const fs::path& root, std::string& error) {
    std::error_code ec;
    if (!fs::exists(root, ec)) return true;
    if (ec) { error = "Cannot inspect project folder: " + ec.message(); return false; }
    for (fs::recursive_directory_iterator it(root, fs::directory_options::none, ec), end;
          it != end; it.increment(ec)) {
        if (ec) { error = "Cannot inspect project resources: " + ec.message(); return false; }
        if (it->is_symlink(ec)) { error = "Symbolic links are not allowed in exported resources"; return false; }
        if (ec) { error = "Cannot inspect project resource: " + ec.message(); return false; }
        if (!it->is_regular_file(ec) && !it->is_directory(ec)) {
            error = "Unsupported filesystem entry in project resources"; return false;
        }
    }
    return true;
}
bool copy_tree(const fs::path& source, const fs::path& target, std::string& error) {
    std::error_code ec;
    if (!fs::exists(source, ec)) return true;
    fs::create_directories(target, ec);
    if (ec) { error = "Cannot create package folder: " + ec.message(); return false; }
    fs::copy(source, target, fs::copy_options::recursive, ec);
    if (ec) { error = "Cannot copy project resources: " + ec.message(); return false; }
    return true;
}
bool valid_platform(ExportPlatform platform) noexcept {
    return platform == ExportPlatform::windows || platform == ExportPlatform::linux;
}
bool valid_build_type(BuildType type) noexcept {
    return type == BuildType::debug || type == BuildType::release;
}
bool exists_file(const fs::path& path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

// "cmake.exe" on Windows, "cmake" elsewhere.
fs::path cmake_binary_name() {
#ifdef _WIN32
    return fs::path("cmake.exe");
#else
    return fs::path("cmake");
#endif
}

// Looks for <dir>/dependencies/cmake/<version>/bin/<cmake> walking up from
// `start`. The newest version directory wins, matching what the README ships.
fs::path bundled_cmake(const fs::path& start) {
    std::error_code ec;
    auto dir = start.empty() ? fs::current_path(ec) : start;
    for (int depth = 0; depth < 8 && !dir.empty(); ++depth) {
        const auto base = dir / "dependencies" / "cmake";
        if (fs::is_directory(base, ec)) {
            fs::path best;
            for (const auto& entry : fs::directory_iterator(base, ec)) {
                const auto candidate = entry.path() / "bin" / cmake_binary_name();
                if (exists_file(candidate) &&
                    (best.empty() || entry.path().filename() > best.parent_path().parent_path().filename()))
                    best = candidate;
            }
            if (!best.empty()) return best;
        }
        const auto parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    return {};
}

// First CMake on PATH. Splitting is done by hand because the separator differs
// between Windows (';') and POSIX (':').
fs::path system_cmake() {
    const char* const value = std::getenv("PATH");
    if (!value) return {};
    const std::string paths = value;
    const char separator =
#ifdef _WIN32
        ';';
#else
        ':';
#endif
    std::size_t begin = 0;
    while (begin <= paths.size()) {
        const std::size_t end = paths.find(separator, begin);
        const std::string entry =
            paths.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        if (!entry.empty()) {
            const auto candidate = fs::path(entry) / cmake_binary_name();
            if (exists_file(candidate)) return candidate;
        }
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return {};
}

// Runs a command with its output redirected to log_file. std::system() mangles
// quoted program paths through cmd.exe, so the Windows path uses CreateProcess.
#ifdef _WIN32
bool run_command(const std::string& executable, const std::string& arguments, const fs::path& log_file) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE log = CreateFileW(log_file.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) return false;

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = log;
    si.hStdError = log;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    const auto to_wide = [](const std::string& text) {
        const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
        std::wstring wide(static_cast<std::size_t>(size > 0 ? size - 1 : 0), L'\0');
        if (size > 1) MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), size);
        return wide;
    };
    std::wstring cmdline = L"\"" + fs::path(executable).wstring() + L"\" " + to_wide(arguments);
    std::vector<wchar_t> buffer(cmdline.begin(), cmdline.end());
    buffer.push_back(L'\0');

    PROCESS_INFORMATION pi{};
    const BOOL started = CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(log);
    if (!started) return false;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return exit_code == 0;
}
#else
bool run_command(const std::string& executable, const std::string& arguments, const fs::path& log_file) {
    const std::string full = "\"" + executable + "\" " + arguments + " > \"" + log_file.string() + "\" 2>&1";
    return std::system(full.c_str()) == 0;
}
#endif
std::string log_tail(const fs::path& log_file, std::size_t max_lines = 12) {
    std::ifstream in(log_file);
    if (!in) return {};
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) lines.push_back(line);
    if (lines.empty()) return {};
    const auto begin = lines.size() > max_lines ? lines.end() - static_cast<std::ptrdiff_t>(max_lines) : lines.begin();
    std::ostringstream out;
    for (auto it = begin; it != lines.end(); ++it) out << *it << '\n';
    return out.str();
}
std::string quoted(const fs::path& path) {
    // CMake accepts forward slashes on Windows and needs them for plain strings.
    std::string text = path.generic_string();
    return "\"" + text + "\"";
}
std::string cmake_string(const fs::path& path) {
    return path.generic_string();
}
// Splits a "A=1;B=2" macro list into its parts, tolerating whitespace.
std::vector<std::string> split_macros(const std::string& text) {
    std::vector<std::string> out;
    std::string current;
    for (const char c : text) {
        if (c == ';' || c == '\n' || c == '\r') {
            if (!current.empty()) out.push_back(current);
            current.clear();
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c))) continue;
        current.push_back(c);
    }
    if (!current.empty()) out.push_back(current);
    return out;
}

// Writes the runtime build project and compiles the game into the package.
// Returns the produced binary path in `binary` on success.
bool build_game_runtime(const fs::path& destination, const BuildSettings& settings,
                        const ExportToolchain& toolchain, const std::string& project_descriptor,
                        fs::path& binary, std::string& error) {
    if (!exists_file(toolchain.cmake)) { error = "Export toolchain is incomplete: cmake not found"; return false; }
    if (!exists_file(toolchain.gridengine_static_lib)) { error = "Export toolchain is incomplete: engine library not found"; return false; }
    if (!exists_file(toolchain.sdl_static_lib)) { error = "Export toolchain is incomplete: SDL library not found"; return false; }
    if (!exists_file(toolchain.game_runtime_source)) { error = "Export toolchain is incomplete: runtime source not found"; return false; }

    std::error_code ec;
    const auto runtime_dir = destination / "runtime";
    fs::create_directories(runtime_dir, ec);
    if (ec) { error = "Cannot create runtime folder: " + ec.message(); return false; }
    fs::copy_file(toolchain.game_runtime_source, runtime_dir / "game_main.cpp", fs::copy_options::overwrite_existing, ec);
    if (ec) { error = "Cannot copy runtime source: " + ec.message(); return false; }

    std::ofstream cmake(runtime_dir / "CMakeLists.txt", std::ios::binary);
    if (!cmake) { error = "Cannot write runtime CMakeLists.txt"; return false; }
    cmake << "cmake_minimum_required(VERSION 3.20)\n"
          << "project(GridEngineGame LANGUAGES CXX)\n"
          << "set(CMAKE_CXX_STANDARD 20)\n"
          << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
          << "add_executable(GridEngineGame game_main.cpp)\n"
          << "target_include_directories(GridEngineGame PRIVATE \""
          << cmake_string(toolchain.engine_include_dir) << "\" \""
          << cmake_string(toolchain.sdl_include_dir) << "\")\n"
          << "target_link_libraries(GridEngineGame PRIVATE \""
          << cmake_string(toolchain.gridengine_static_lib) << "\" \""
          << cmake_string(toolchain.sdl_static_lib) << "\")\n";
    // The engine library was compiled with its feature macros; the runtime has
    // to use the same set so its behavior matches the linked engine code.
    for (const auto& macro : split_macros(toolchain.feature_macros)) {
        if (macro.empty()) continue;
        cmake << "target_compile_definitions(GridEngineGame PRIVATE " << macro << ")\n";
    }
    cmake << "if(WIN32)\n"
          << "    target_link_libraries(GridEngineGame PRIVATE opengl32 imm32 winmm ole32\n"
          << "        oleaut32 version setupapi shell32)\n"
          << "endif()\n";
    if (!cmake) { error = "Failed while writing runtime CMakeLists.txt"; return false; }
    // The configure step must observe the complete file, so flush and close
    // before invoking the toolchain.
    cmake.flush();
    cmake.close();
    if (!cmake) { error = "Failed while writing runtime CMakeLists.txt"; return false; }

    const auto build_dir = destination / "runtime-build";
    const auto log = destination / "runtime-build.log";
    const char* config = settings.type == BuildType::release ? "Release" : "Debug";

    std::string configure_args = "-S " + quoted(runtime_dir) + " -B " + quoted(build_dir);
    if (!toolchain.generator.empty()) configure_args += " -G \"" + toolchain.generator + "\"";
    configure_args += " -DCMAKE_BUILD_TYPE=" + std::string(config);
    if (!run_command(toolchain.cmake.string(), configure_args, log)) {
        error = "Cannot configure the game runtime build: " + configure_args + "\n" + log_tail(log);
        return false;
    }
    const std::string build_args =
        "--build " + quoted(build_dir) + " --config " + config + " --parallel";
    if (!run_command(toolchain.cmake.string(), build_args, log)) {
        error = "Cannot compile the game runtime: " + build_args + "\n" + log_tail(log);
        return false;
    }

    // Multi-config generators place the binary in a per-config subfolder.
    fs::path exe = build_dir / config / "GridEngineGame.exe";
    if (!exists_file(exe)) exe = build_dir / "GridEngineGame.exe";
    if (!exists_file(exe)) {
        for (const auto& entry : fs::recursive_directory_iterator(build_dir, ec)) {
            if (entry.is_regular_file(ec) && entry.path().filename() == "GridEngineGame.exe") {
                exe = entry.path();
                break;
            }
        }
    }
    if (!exists_file(exe)) { error = "The game runtime did not produce GridEngineGame.exe"; return false; }
    std::error_code bin_ec;
    fs::create_directories(destination / "bin", bin_ec);
    if (bin_ec) { error = "Cannot create package bin folder: " + bin_ec.message(); return false; }
    fs::copy_file(exe, destination / "bin" / "GridEngineGame.exe", fs::copy_options::overwrite_existing, bin_ec);
    if (bin_ec) { error = "Cannot install the game executable: " + bin_ec.message(); return false; }
    binary = destination / "bin" / "GridEngineGame.exe";
    (void)project_descriptor;
    return true;
}
} // namespace

std::string_view export_platform_name(ExportPlatform p) noexcept {
    switch (p) { case ExportPlatform::windows: return "windows"; case ExportPlatform::linux: return "linux"; }
    return "unknown";
}
std::string_view build_type_name(BuildType t) noexcept {
    switch (t) { case BuildType::debug: return "debug"; case BuildType::release: return "release"; }
    return "unknown";
}

bool ExportToolchain::resolvable() const {
    return exists_file(cmake) && exists_file(gridengine_static_lib) &&
           exists_file(sdl_static_lib) && exists_file(game_runtime_source);
}

fs::path find_cmake_executable(const fs::path& start_dir) {
    if (const auto bundled = bundled_cmake(start_dir); !bundled.empty()) return bundled;
    return system_cmake();
}

ExportResult export_project(const fs::path& project_file, const fs::path& destination,
                            const BuildSettings& s, const ExportToolchain* toolchain) {
    if (destination.empty()) return {{}, "Export destination is empty"};
    if (s.profile.empty() || s.profile.find_first_of("=\r\n") != std::string::npos ||
        s.profile.find('\0') != std::string::npos)
        return {{}, "Build profile must be a non-empty single line without '='"};
    if (!valid_platform(s.platform) || !valid_build_type(s.type))
        return {{}, "Unsupported export platform or build type"};
    if (s.width == 0 || s.height == 0 || s.width > 16384 || s.height > 16384)
        return {{}, "Window dimensions must be between 1 and 16384"};
    if (s.fps_limit > 1000) return {{}, "FPS limit must be 0 (uncapped) or at most 1000"};
    if (s.build_executable && !toolchain)
        return {{}, "Requested an executable build but no export toolchain was provided"};
    auto project = open_project(project_file);
    if (!project) return {{}, "Invalid project: " + project.error};
    std::error_code ec;
    auto out_abs = fs::absolute(destination, ec).lexically_normal();
    if (ec) return {{}, "Cannot resolve export destination: " + ec.message()};
    auto root_abs = fs::absolute(project_file.parent_path(), ec).lexically_normal();
    if (ec) return {{}, "Cannot resolve project folder: " + ec.message()};
    auto relative = out_abs.lexically_relative(root_abs);
    if (!relative.empty() && *relative.begin() != "..")
        return {{}, "Export destination cannot be inside the source project"};
    if (fs::exists(destination, ec)) return {{}, "Export destination already exists; refusing to overwrite"};
    if (ec) return {{}, "Cannot inspect export destination: " + ec.message()};
    const auto root = project_file.parent_path();
    for (const auto* folder : {"assets", "scenes"})
        if (!safe_tree(root / folder, project.error)) return {{}, project.error};
    fs::create_directories(destination, ec);
    if (ec) return {{}, "Cannot create export destination: " + ec.message()};
    auto fail = [&](std::string message) {
        // GRIDENGINE_KEEP_FAILED_EXPORT=1 preserves the failed package (and its
        // build log) for debugging instead of cleaning it up.
        const auto keep = std::getenv("GRIDENGINE_KEEP_FAILED_EXPORT");
        if (!keep || keep[0] != '1') {
            std::error_code ignored; fs::remove_all(destination, ignored);
        }
        return ExportResult{{}, std::move(message)};
    };
    fs::copy_file(project_file, destination / project_file.filename(), fs::copy_options::none, ec);
    if (ec) return fail("Cannot copy project descriptor: " + ec.message());
    for (const auto* folder : {"assets", "scenes"})
        if (!copy_tree(root / folder, destination / folder, project.error)) return fail(project.error);

    fs::path binary;
    if (s.build_executable) {
        if (!build_game_runtime(destination, s, *toolchain, project_file.filename().string(), binary, project.error))
            return fail(project.error);
    }

    std::ofstream manifest(destination / "build.gridcfg", std::ios::binary);
    if (!manifest) return fail("Cannot write build settings");
    manifest << "GridEngineBuild 1\n"
             << "project=" << project_file.filename().string() << "\n"
             << "profile=" << s.profile << "\n"
             << "platform=" << export_platform_name(s.platform) << "\n"
             << "type=" << build_type_name(s.type) << "\n"
             << "window_width=" << s.width << "\nwindow_height=" << s.height << "\n"
             << "vsync=" << (s.vsync ? "true" : "false") << "\nfps_limit=" << s.fps_limit << "\n"
             << "binary_included=" << (s.build_executable ? "true" : "false") << "\n";
    if (!manifest) return fail("Failed while writing build settings");
    return {destination, {}};
}
} // namespace gridengine
