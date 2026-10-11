// GridEngine - src/engine.cpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#include "gridengine/engine.hpp"

namespace gridengine {

Engine::Engine(EngineConfig config) noexcept : config_(config) {}

bool Engine::initialize() noexcept {
    if (config_.target_fps == 0 || config_.title.empty()) {
        return false;
    }
    initialized_ = true;
    return true;
}

void Engine::shutdown() noexcept {
    initialized_ = false;
}

std::string_view version() noexcept {
    return "0.9.0";
}

} // namespace gridengine
