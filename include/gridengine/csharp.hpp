// GridEngine - include/gridengine/csharp.hpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#pragma once
#include "gridengine/scene.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

// C# scripting integration.
//
// Grid covers isolated numeric logic; C# is added for anything that needs a
// richer language. The bridge is a small generated console project that the
// engine builds with `dotnet build` and drives line by line:
//
//   engine -> "frame <id> <x> <y> <rotation> <delta>"
//   bridge -> "ok <x> <y> <rotation>"          (or "error <message>")
//
// Each step processes one entity for one frame, so a behavior is a function of
// (entity frame state) -> (entity frame state) and can keep internal state.

namespace gridengine {

struct CSharpFrameResult {
    bool ok{false};
    double x{};
    double y{};
    double rotation{};
    std::string error;
};

class CSharpBridge final {
public:
    CSharpBridge();
    ~CSharpBridge();
    CSharpBridge(const CSharpBridge&) = delete;
    CSharpBridge& operator=(const CSharpBridge&) = delete;

    // Generates and compiles the C# project into `project_dir`.
    [[nodiscard]] bool build(const std::filesystem::path& project_dir,
                             std::string assembly_name = "GridBehavior");
    // Starts the compiled bridge process; requires a successful build().
    [[nodiscard]] bool start();
    // Steps one entity one frame. False means the bridge is unusable.
    [[nodiscard]] bool step(std::uint64_t id, double x, double y, double rotation,
                            double delta, CSharpFrameResult& out);
    void stop();
    [[nodiscard]] bool running() const;
    [[nodiscard]] const std::string& error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace gridengine
