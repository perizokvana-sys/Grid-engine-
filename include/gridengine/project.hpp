// GridEngine - include/gridengine/project.hpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace gridengine {

struct ProjectInfo {
    std::string name;
    std::string engine_version{"0.9.0"};
    std::filesystem::path file;
};

struct ProjectResult {
    ProjectInfo project;
    std::string error;
    [[nodiscard]] explicit operator bool() const noexcept { return error.empty(); }
};

// Creates a project folder with a portable text descriptor and starter folders.
[[nodiscard]] ProjectResult create_project(const std::filesystem::path& directory,
                                           std::string_view name);
// Opens and validates a .grid project descriptor.
[[nodiscard]] ProjectResult open_project(const std::filesystem::path& file);

} // namespace gridengine
