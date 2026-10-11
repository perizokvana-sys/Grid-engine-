// GridEngine - include/gridengine/engine.hpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#pragma once

#include <chrono>
#include <cstdint>
#include <string_view>

namespace gridengine {

struct EngineConfig {
    std::string_view title{"GridEngine"};
    std::uint32_t target_fps{60};
};

// Small lifecycle shell; rendering, platform windows and game systems are added
// as explicit modules in later milestones.
class Engine final {
public:
    explicit Engine(EngineConfig config = {}) noexcept;

    [[nodiscard]] const EngineConfig& config() const noexcept { return config_; }
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }

    [[nodiscard]] bool initialize() noexcept;
    void shutdown() noexcept;

private:
    EngineConfig config_;
    bool initialized_{false};
};

[[nodiscard]] std::string_view version() noexcept;

} // namespace gridengine
