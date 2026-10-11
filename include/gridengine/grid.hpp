// GridEngine - include/gridengine/grid.hpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gridengine { class Scene; }

namespace gridengine::grid {

struct Diagnostic {
    std::string message;
    std::size_t offset{};
    std::size_t line{1};
    std::size_t column{1};
};
struct RunResult {
    bool success{};
    std::vector<double> printed;
    Diagnostic diagnostic;
};

// Executes isolated Grid code. The no-delta form supplies delta = 0.
[[nodiscard]] RunResult run(const std::string& source);
// Executes isolated Grid code with a read-only frame delta in seconds.
[[nodiscard]] RunResult run(const std::string& source, double delta_seconds);
// Restricted binding exposing delta and position_x/position_y. Transform writes commit atomically.
[[nodiscard]] RunResult run_on_entity(const std::string& source, Scene& scene,
                                      std::uint64_t entity_id, double delta_seconds);

} // namespace gridengine::grid
