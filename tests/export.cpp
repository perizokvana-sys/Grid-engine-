// GridEngine - tests/export.cpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#include "gridengine/export.hpp"
#include "gridengine/project.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

int main() {
    namespace fs = std::filesystem;
    const auto base = fs::temp_directory_path() / "gridengine_export_tests_v09";
    std::error_code ec;
    fs::remove_all(base, ec);
    const auto source = base / "project";
    auto created = gridengine::create_project(source, "Export Test");
    assert(created);
    fs::create_directories(source / "assets" / "nested");
    fs::create_directories(source / "scenes");
    { std::ofstream(source / "assets" / "nested" / "data.txt") << "asset"; }
    { std::ofstream(source / "scenes" / "main.scene") << "scene"; }

    gridengine::BuildSettings settings;
    settings.profile = "Windows Release";
    settings.platform = gridengine::ExportPlatform::windows;
    settings.type = gridengine::BuildType::release;
    settings.width = 1920;
    settings.height = 1080;
    settings.vsync = false;
    settings.fps_limit = 144;
    const auto output = base / "package";
    auto result = gridengine::export_project(created.project.file, output, settings);
    assert(result);
    assert(fs::exists(output / created.project.file.filename()));
    assert(fs::exists(output / "assets" / "nested" / "data.txt"));
    assert(fs::exists(output / "scenes" / "main.scene"));
    assert(fs::exists(output / "build.gridcfg"));
    std::ifstream manifest(output / "build.gridcfg");
    const std::string text((std::istreambuf_iterator<char>(manifest)), {});
    assert(text.find("platform=windows") != std::string::npos);
    assert(text.find("type=release") != std::string::npos);
    assert(text.find("window_width=1920") != std::string::npos);
    assert(text.find("vsync=false") != std::string::npos);
    assert(text.find("binary_included=false") != std::string::npos);

    assert(!gridengine::export_project(created.project.file, output, settings));
    assert(!gridengine::export_project(created.project.file, source / "inside", settings));
    settings.width = 0;
    assert(!gridengine::export_project(created.project.file, base / "invalid", settings));
    settings.width = 1280;
    settings.profile = "bad\nprofile";
    assert(!gridengine::export_project(created.project.file, base / "invalid2", settings));
    settings.profile = "build\ninjected=true";
    assert(!gridengine::export_project(created.project.file, base / "injected", settings));
    settings.profile = "valid=override";
    assert(!gridengine::export_project(created.project.file, base / "override", settings));
    settings.profile = "valid";
    settings.platform = static_cast<gridengine::ExportPlatform>(99);
    assert(!gridengine::export_project(created.project.file, base / "bad-platform", settings));
    settings.platform = gridengine::ExportPlatform::windows;
    settings.type = static_cast<gridengine::BuildType>(99);
    assert(!gridengine::export_project(created.project.file, base / "bad-type", settings));
    fs::remove_all(base, ec);
}
