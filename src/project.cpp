// GridEngine - src/project.cpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#include "gridengine/project.hpp"
#include "gridengine/engine.hpp"

#include <cctype>
#include <fstream>
#include <system_error>
#include <utility>

namespace gridengine {
namespace {
bool valid_name(std::string_view name) {
    if (name.empty() || name.size() > 80) return false;
    for (unsigned char c : name) {
        if (!(std::isalnum(c) || c == ' ' || c == '-' || c == '_')) return false;
    }
    return true;
}
std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r");
    if (first == std::string::npos) return {};
    return value.substr(first, value.find_last_not_of(" \t\r") - first + 1);
}
}

ProjectResult create_project(const std::filesystem::path& directory, std::string_view name) {
    if (!valid_name(name)) return {{}, "Project name must be 1-80 letters, digits, spaces, '-' or '_'"};
    if (directory.empty()) return {{}, "Project directory is empty"};
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) return {{}, "Cannot create project directory: " + ec.message()};
    const auto descriptor = directory / (std::string(name) + ".grid");
    if (std::filesystem::exists(descriptor, ec)) return {{}, "Project descriptor already exists"};
    std::filesystem::create_directories(directory / "scenes", ec);
    if (ec) return {{}, "Cannot create scenes folder: " + ec.message()};
    std::filesystem::create_directories(directory / "assets", ec);
    if (ec) return {{}, "Cannot create assets folder: " + ec.message()};

    std::ofstream out(descriptor, std::ios::binary);
    if (!out) return {{}, "Cannot write project descriptor"};
    out << "GridEngineProject\nversion=1\nname=" << name << "\nengine=" << version() << "\n";
    if (!out) return {{}, "Failed while writing project descriptor"};
    return {{std::string(name), std::string(version()), descriptor}, {}};
}

ProjectResult open_project(const std::filesystem::path& file) {
    if (file.extension() != ".grid") return {{}, "Project file must have .grid extension"};
    std::ifstream in(file, std::ios::binary);
    if (!in) return {{}, "Cannot open project file"};
    std::string line;
    if (!std::getline(in, line) || line != "GridEngineProject") return {{}, "Invalid project descriptor signature"};
    ProjectInfo info;
    info.file = file;
    bool version_seen = false;
    while (std::getline(in, line)) {
        line = trim(std::move(line));
        const auto separator = line.find('=');
        if (separator == std::string::npos) continue;
        const auto key = line.substr(0, separator);
        const auto value = line.substr(separator + 1);
        if (key == "name") info.name = value;
        else if (key == "engine") info.engine_version = value;
        else if (key == "version" && value == "1") version_seen = true;
    }
    if (!version_seen || !valid_name(info.name)) return {{}, "Project descriptor is incomplete or unsupported"};
    return {std::move(info), {}};
}
} // namespace gridengine
