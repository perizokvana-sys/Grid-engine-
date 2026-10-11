// GridEngine - tests/particles.cpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#include "gridengine/particles.hpp"
#include <cassert>
#include <cmath>
#include <limits>
using namespace gridengine;
int main() {
    ParticleEmitter2D e;
    e.particles_per_second = 4.0;
    e.particle_lifetime = 0.5;
    e.initial_velocity = {2.0, 0.0};
    e.velocity_spread = {0.0, 1.0};
    e.start_size = 2.0;
    e.end_size = 0.0;
    assert(e.update(0.25, {10.0, 20.0}));
    assert(e.particles.size() == 1);
    assert(e.particles[0].position.x == 10.0);
    assert(e.update(0.25, {10.0, 20.0}));
    assert(e.particles.size() == 2);
    assert(std::abs(e.particles[0].position.x - 10.5) < 1e-9);
    assert(std::abs(e.particles[0].size() - 1.0) < 1e-9);
    assert(e.update(0.5, {0.0, 0.0}));
    assert(e.particles.size() == 2); // expired particles replaced
    e.emitting = false;
    assert(e.update(0.5, {0.0, 0.0}));
    assert(e.particles.empty());
    e.max_particles = 1;
    e.emitting = true;
    assert(e.update(1.0, {0.0, 0.0}));
    assert(e.particles.size() == 1);
    const auto before = e.particles.size();
    assert(!e.update(-1.0, {}));
    assert(e.particles.size() == before);
    e.particles_per_second = std::numeric_limits<double>::infinity();
    assert(!e.update(0.1, {}));
    e.clear();
    assert(e.particles.empty() && e.emission_remainder == 0.0);

    // Huge finite bursts are capped without an out-of-range integer conversion.
    ParticleEmitter2D capped;
    capped.max_particles = 3;
    capped.particles_per_second = std::numeric_limits<double>::max();
    assert(capped.update(1.0, {}));
    assert(capped.particles.size() == 3);

    // Overflow in generated velocity fails atomically.
    ParticleEmitter2D overflow;
    overflow.particles_per_second = 2.0;
    overflow.initial_velocity.x = std::numeric_limits<double>::max();
    overflow.velocity_spread.x = std::numeric_limits<double>::max();
    assert(!overflow.update(1.0, {}));
    assert(overflow.particles.empty() && overflow.emission_remainder == 0.0);
}
