// C# scripting integration test.
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.
//
// Builds and runs the generated C# behavior bridge through the engine API and
// checks the round trip: one frame of the sample behavior must move the entity
// 100 units on X and rotate it 90 degrees at delta = 1s.
//
// Skipped (exit 0) when the dotnet SDK is not installed.

#include "gridengine/csharp.hpp"
#include "gridengine/scene.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace {
namespace fs = std::filesystem;
} // namespace

int main() {
    const auto workspace = fs::temp_directory_path() / "gridengine_csharp_test";
    std::error_code ec;
    fs::remove_all(workspace, ec);
    fs::create_directories(workspace, ec);

    gridengine::CSharpBridge bridge;
    if (!bridge.build(workspace / "csharp", "GridBehavior")) {
        const auto& error = bridge.error();
        if (error.find("dotnet SDK not available") != std::string::npos ||
            error.find("only implemented on Windows") != std::string::npos) {
            std::cout << "gridengine_csharp: skipped (" << error << ")\n";
            fs::remove_all(workspace, ec);
            return 0;
        }
        std::cerr << "FATAL: building the C# bridge failed: " << error << '\n';
        fs::remove_all(workspace, ec);
        return 1;
    }
    if (!bridge.start()) {
        std::cerr << "FATAL: starting the C# bridge failed: " << bridge.error() << '\n';
        fs::remove_all(workspace, ec);
        return 1;
    }

    gridengine::CSharpFrameResult frame;
    if (!bridge.step(1, 0.0, 5.0, 0.0, 1.0, frame) || !frame.ok) {
        std::cerr << "FATAL: the C# bridge did not answer a frame: " << bridge.error() << '\n';
        bridge.stop();
        fs::remove_all(workspace, ec);
        return 1;
    }
    if (std::abs(frame.x - 100.0) > 1e-6 || std::abs(frame.y - 5.0) > 1e-6 ||
        std::abs(frame.rotation - 90.0) > 1e-6) {
        std::cerr << "FATAL: unexpected bridge state x=" << frame.x << " y=" << frame.y
                  << " rotation=" << frame.rotation << '\n';
        bridge.stop();
        fs::remove_all(workspace, ec);
        return 1;
    }

    // Apply the bridge result to a scene exactly like the runtime does.
    gridengine::Scene scene;
    const auto entity = scene.create_entity("CSharp");
    if (!entity) { std::cerr << "FATAL: scene setup failed\n"; return 1; }
    scene.find(entity)->transform.position.x = 0.0;
    scene.find(entity)->transform.position.y = 5.0;
    scene.find(entity)->transform.rotation = 0.0;
    if (!bridge.step(entity, 0.0, 5.0, 0.0, 1.0, frame) || !frame.ok) {
        std::cerr << "FATAL: the C# bridge failed the second frame\n";
        bridge.stop();
        fs::remove_all(workspace, ec);
        return 1;
    }
    scene.find(entity)->transform.position.x = frame.x;
    scene.find(entity)->transform.position.y = frame.y;
    scene.find(entity)->transform.rotation = frame.rotation;

    if (std::abs(scene.find(entity)->transform.position.x - 100.0) > 1e-6) {
        std::cerr << "FATAL: the scene was not advanced by the C# behavior\n";
        bridge.stop();
        fs::remove_all(workspace, ec);
        return 1;
    }

    // A negative frame time is a protocol violation: the bridge must report it
    // instead of applying it.
    gridengine::CSharpFrameResult bad;
    if (bridge.step(1, 0.0, 0.0, 0.0, -1.0, bad) || bad.ok || bad.error.empty()) {
        std::cerr << "FATAL: the bridge accepted an invalid frame\n";
        bridge.stop();
        fs::remove_all(workspace, ec);
        return 1;
    }

    bridge.stop();
    fs::remove_all(workspace, ec);
    std::cout << "gridengine_csharp: ok (x=" << scene.find(entity)->transform.position.x
              << " rotation=" << scene.find(entity)->transform.rotation << ")\n";
    return 0;
}
