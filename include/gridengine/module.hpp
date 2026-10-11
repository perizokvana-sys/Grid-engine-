// GridEngine - include/gridengine/module.hpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#pragma once
#include "gridengine/scene.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

// Native C++ module integration.
//
// A module is a platform shared library exporting `grid_module_entry`:
//
//     extern "C" __declspec(dllexport) std::uint64_t
//     grid_module_entry(const gridengine::EngineApi* api) {
//         static gridengine::Behavior spin{...};
//         api->register_behavior(&spin);
//         return 1;
//     }
//
// The engine calls the entry once on load, then invokes every registered
// behavior for each entity every frame. Modules share the engine's C++ ABI, so
// they must be compiled with the same engine headers and standard library.
//
// Grid scripts stay numeric and side-effect free; anything that needs engine
// access, complex types or the file system is written as a native module.

namespace gridengine {

using ModuleHandle = std::uint64_t;

struct EngineApi;

// A behavior the engine invokes for every entity, every frame.
struct Behavior {
    const char* name;
    void (*update)(const EngineApi& api, void* context, Scene& scene, std::uint64_t id, double delta);
};

// Function table the engine hands to every module on load.
struct EngineApi {
    std::uint64_t version{1};
    void* context{nullptr}; // the loader instance; pass it back to the callbacks
    // Registers `behavior`; returns false when it is already registered.
    bool (*register_behavior)(void* context, Behavior* behavior);
    // Engine services available to behaviors.
    void (*log)(void* context, const char* message);
    Scene& (*scene)(void* context);
    void (*set_position)(Scene& scene, std::uint64_t id, double x, double y);
    void (*set_rotation)(Scene& scene, std::uint64_t id, double degrees);
    bool (*world_position)(const Scene& scene, std::uint64_t id, double* x, double* y);
};

// Entry point every module must export.
using ModuleEntry = ModuleHandle (*)(const EngineApi* api);

// Loads native modules and drives their behaviors.
class ModuleLoader final {
public:
    ModuleLoader();
    ~ModuleLoader();
    ModuleLoader(const ModuleLoader&) = delete;
    ModuleLoader& operator=(const ModuleLoader&) = delete;

    // Loads every shared library in `directory` (non-recursive). Returns the
    // number of modules that loaded successfully.
    [[nodiscard]] std::size_t load_directory(const std::filesystem::path& directory);
    // Invokes every registered behavior for one entity. No-op when there are none.
    void run_behaviors(Scene& scene, std::uint64_t id, double delta);
    [[nodiscard]] std::size_t behavior_count() const noexcept;
    [[nodiscard]] std::size_t module_count() const noexcept;
    [[nodiscard]] const std::vector<const char*>& failed_modules() const noexcept;

private:
    struct Impl;
    Impl* impl_{nullptr};
};

} // namespace gridengine
