// GridEngine - src/csharp.cpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#include "gridengine/csharp.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace gridengine {
namespace {
#ifdef _WIN32
// Runs a program with its output captured into `output`; returns the exit code.
int run_and_capture(const std::string& command_line, std::string& output) {
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
    const std::wstring command = wide(command_line);
    std::vector<wchar_t> buffer(command.begin(), command.end());
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

// The generated C# program. Kept minimal so it works on any modern SDK and can
// be replaced by project-specific logic.
std::string csharp_source() {
    return R"CSHARP(using System.Globalization;

// GridEngine C# behavior bridge.
// Protocol (one entity per line, one line back):
//   "frame <id> <x> <y> <rotation> <delta>"  ->  "ok <x> <y> <rotation>"
static string F(double v) => v.ToString("R", CultureInfo.InvariantCulture);

while (true) {
    string line = Console.In.ReadLine();
    if (line is null) break;
    string[] p = line.Split(' ');
    if (p.Length < 6 || p[0] != "frame") {
        Console.WriteLine("error bad frame");
        Console.Out.Flush();
        continue;
    }
    double x = double.Parse(p[2], CultureInfo.InvariantCulture);
    double y = double.Parse(p[3], CultureInfo.InvariantCulture);
    double rot = double.Parse(p[4], CultureInfo.InvariantCulture);
    double dt = double.Parse(p[5], CultureInfo.InvariantCulture);
    if (double.IsNaN(x) || double.IsNaN(y) || double.IsNaN(rot) || double.IsNaN(dt) ||
        double.IsInfinity(x) || double.IsInfinity(y) || double.IsInfinity(rot) || double.IsInfinity(dt) ||
        dt < 0) {
        Console.WriteLine("error invalid frame values");
        Console.Out.Flush();
        continue;
    }
    // Sample behavior: drift right at 100 units/s and spin at 90 degrees/s.
    x += 100.0 * dt;
    rot += 90.0 * dt;
    Console.WriteLine($"ok {F(x)} {F(y)} {F(rot)}");
    Console.Out.Flush();
}
)CSHARP";
}
#else
[[maybe_unused]] int run_and_capture(const std::string&, std::string& output) {
    output = "C# bridge is only implemented on Windows";
    return -1;
}
[[maybe_unused]] std::string quote(const std::string&) { return {}; }
[[maybe_unused]] std::string csharp_source() { return {}; }
#endif
} // namespace

struct CSharpBridge::Impl {
    std::string error;
    std::filesystem::path project_dir;
    std::filesystem::path dll;
    bool started{false};
#ifdef _WIN32
    PROCESS_INFORMATION process{};
    HANDLE stdin_write{nullptr};
    HANDLE stdout_read{nullptr};
#endif
    ~Impl() { stop(); }

    void stop() {
#ifdef _WIN32
        if (process.hProcess) {
            TerminateProcess(process.hProcess, 0);
            CloseHandle(process.hProcess);
            CloseHandle(process.hThread);
            process.hProcess = nullptr;
        }
        if (stdin_write) { CloseHandle(stdin_write); stdin_write = nullptr; }
        if (stdout_read) { CloseHandle(stdout_read); stdout_read = nullptr; }
#endif
        started = false;
    }
};

CSharpBridge::CSharpBridge() = default;
CSharpBridge::~CSharpBridge() = default;

bool CSharpBridge::build(const std::filesystem::path& project_dir, std::string assembly_name) {
    impl_ = std::make_unique<Impl>();
    impl_->project_dir = project_dir;
#ifdef _WIN32
    std::error_code ec;
    std::filesystem::create_directories(project_dir, ec);
    if (ec) {
        impl_->error = "Cannot create the C# project directory: " + ec.message();
        return false;
    }

    // Pick the target framework from the installed SDK version so the project
    // works with any recent dotnet SDK.
    std::string version_output;
    if (run_and_capture("dotnet --version", version_output) != 0) {
        impl_->error = "dotnet SDK not available: " + version_output;
        return false;
    }
    std::string major;
    for (const char c : version_output) {
        if (std::isdigit(static_cast<unsigned char>(c))) major.push_back(c);
        else break;
    }
    if (major.empty()) {
        impl_->error = "Cannot parse the dotnet SDK version: " + version_output;
        return false;
    }
    const std::string target_framework = "net" + major + ".0";

    std::string normalized = assembly_name.empty() ? "GridBehavior" : assembly_name;
    std::replace_if(normalized.begin(), normalized.end(),
                    [](unsigned char c) { return !(std::isalnum(c) || c == '_'); }, '_');
    impl_->dll = project_dir / "bin" / "Release" / target_framework / (normalized + ".dll");

    {
        std::ofstream csproj(project_dir / (normalized + ".csproj"), std::ios::binary);
        if (!csproj) { impl_->error = "Cannot write the C# project file"; return false; }
        csproj << "<Project Sdk=\"Microsoft.NET.Sdk\">\n"
               << "  <PropertyGroup>\n"
               << "    <OutputType>Exe</OutputType>\n"
               << "    <TargetFramework>" << target_framework << "</TargetFramework>\n"
               << "    <AssemblyName>" << normalized << "</AssemblyName>\n"
               << "    <ImplicitUsings>enable</ImplicitUsings>\n"
               << "    <Nullable>disable</Nullable>\n"
               << "    <InvariantGlobalization>true</InvariantGlobalization>\n"
               << "  </PropertyGroup>\n"
               << "</Project>\n";
        csproj.close();
        if (!csproj) { impl_->error = "Failed while writing the C# project file"; return false; }
    }
    {
        std::ofstream program(project_dir / "Program.cs", std::ios::binary);
        if (!program) { impl_->error = "Cannot write the C# program file"; return false; }
        program << csharp_source();
        program.close();
        if (!program) { impl_->error = "Failed while writing the C# program file"; return false; }
    }

    std::string output;
    const std::string command = "dotnet build " + quote(project_dir.string()) +
                                " -c Release --nologo -v minimal";
    if (run_and_capture(command, output) != 0) {
        impl_->error = "dotnet build failed: " + output;
        return false;
    }
    std::error_code exists;
    if (!std::filesystem::is_regular_file(impl_->dll, exists) || exists) {
        impl_->error = "dotnet build did not produce " + impl_->dll.string();
        return false;
    }
    return true;
#else
    (void)assembly_name;
    impl_->error = "The C# bridge is only implemented on Windows";
    return false;
#endif
}

bool CSharpBridge::start() {
    if (!impl_ || impl_->dll.empty()) { if (impl_) impl_->error = "build() must succeed first"; return false; }
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE child_stdin_read = nullptr;
    HANDLE child_stdout_write = nullptr;
    if (!CreatePipe(&impl_->stdout_read, &child_stdout_write, &sa, 0) ||
        !CreatePipe(&child_stdin_read, &impl_->stdin_write, &sa, 0)) {
        impl_->error = "Cannot create the bridge pipes";
        return false;
    }
    SetHandleInformation(impl_->stdout_read, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(impl_->stdin_write, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = child_stdin_read;
    si.hStdOutput = child_stdout_write;
    si.hStdError = child_stdout_write;
    const auto wide = [](const std::string& text) {
        const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
        std::wstring out(static_cast<std::size_t>(size > 0 ? size - 1 : 0), L'\0');
        if (size > 1) MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), size);
        return out;
    };
    const std::wstring command = L"dotnet " + std::filesystem::path(impl_->dll).wstring();
    std::vector<wchar_t> buffer(command.begin(), command.end());
    buffer.push_back(L'\0');
    PROCESS_INFORMATION pi{};
    const BOOL started = CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(child_stdin_read);
    CloseHandle(child_stdout_write);
    if (!started) {
        impl_->error = "Cannot start the bridge process (dotnet on PATH?)";
        return false;
    }
    impl_->process = pi;
    impl_->started = true;
    return true;
#else
    impl_->error = "The C# bridge is only implemented on Windows";
    return false;
#endif
}

bool CSharpBridge::step(std::uint64_t id, double x, double y, double rotation, double delta,
                        CSharpFrameResult& out) {
    out = {};
    if (!impl_ || !impl_->started) { if (impl_) impl_->error = "The bridge is not running"; return false; }
#ifdef _WIN32
    std::ostringstream request;
    request << "frame " << id << ' ' << x << ' ' << y << ' ' << rotation << ' ' << delta << '\n';
    const std::string line = request.str();
    DWORD written = 0;
    if (!WriteFile(impl_->stdin_write, line.data(), static_cast<DWORD>(line.size()), &written, nullptr)) {
        impl_->error = "Cannot write the frame request";
        return false;
    }
    std::string response;
    char chunk = '\0';
    DWORD read_bytes = 0;
    while (true) {
        DWORD available = 0;
        if (!PeekNamedPipe(impl_->stdout_read, nullptr, 0, nullptr, &available, nullptr)) {
            impl_->error = "The bridge closed its output";
            return false;
        }
        if (available == 0) {
            // The child answers one line per request; if it died, fail fast.
            if (WaitForSingleObject(impl_->process.hProcess, 0) == WAIT_OBJECT_0) {
                impl_->error = "The bridge exited unexpectedly";
                return false;
            }
            continue;
        }
        if (!ReadFile(impl_->stdout_read, &chunk, 1, &read_bytes, nullptr) || read_bytes != 1) {
            impl_->error = "The bridge closed its output";
            return false;
        }
        response.push_back(chunk);
        if (chunk == '\n') break;
    }
    while (!response.empty() && (response.back() == '\n' || response.back() == '\r'))
        response.pop_back();

    std::istringstream in(response);
    std::string status;
    in >> status;
    if (status == "ok") {
        if (in >> out.x >> out.y >> out.rotation) { out.ok = true; return true; }
        impl_->error = "Malformed bridge response: " + response;
        return false;
    }
    if (status == "error") {
        std::getline(in, out.error);
        if (!out.error.empty() && out.error.front() == ' ') out.error.erase(out.error.begin());
        impl_->error = "C# behavior error: " + out.error;
        return false;
    }
    impl_->error = "Unknown bridge response: " + response;
    return false;
#else
    (void)id;
    (void)x;
    (void)y;
    (void)rotation;
    (void)delta;
    impl_->error = "The C# bridge is only implemented on Windows";
    return false;
#endif
}

void CSharpBridge::stop() {
    if (impl_) impl_->stop();
}
bool CSharpBridge::running() const { return impl_ && impl_->started; }
const std::string& CSharpBridge::error() const {
    static const std::string empty;
    return impl_ ? impl_->error : empty;
}

} // namespace gridengine
