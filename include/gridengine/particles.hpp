// GridEngine - include/gridengine/particles.hpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#pragma once

#include "gridengine/scene.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace gridengine {

struct Particle2D {
    Vec2 position{};
    Vec2 velocity{};
    double age{0.0};
    double lifetime{1.0};
    std::uint32_t color{0xffffffffu};
    double start_size{1.0};
    double end_size{0.0};
    [[nodiscard]] double normalized_age() const noexcept;
    [[nodiscard]] double size() const noexcept;
};

// Small deterministic emitter. Call update once per frame; emission uses the
// emitter's world position and particles continue in world space.
struct ParticleEmitter2D {
    bool emitting{true};
    double particles_per_second{10.0};
    double particle_lifetime{1.0};
    Vec2 initial_velocity{};
    Vec2 velocity_spread{}; // deterministic symmetric spread, no global RNG
    double start_size{1.0};
    double end_size{0.0};
    std::uint32_t color{0xffffffffu};
    std::size_t max_particles{1024};
    double emission_remainder{0.0};
    std::vector<Particle2D> particles;

    // Invalid parameters or delta return false without changing the emitter.
    [[nodiscard]] bool update(double delta_seconds, Vec2 world_position);
    void clear() noexcept;
};

} // namespace gridengine
