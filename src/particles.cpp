// GridEngine - src/particles.cpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#include "gridengine/particles.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace gridengine {
namespace {
bool finite(Vec2 v) noexcept { return std::isfinite(v.x) && std::isfinite(v.y); }
bool valid(const ParticleEmitter2D& e) noexcept {
    return std::isfinite(e.particles_per_second) && e.particles_per_second >= 0.0 &&
        std::isfinite(e.particle_lifetime) && e.particle_lifetime > 0.0 &&
        finite(e.initial_velocity) && finite(e.velocity_spread) &&
        std::isfinite(e.start_size) && e.start_size >= 0.0 &&
        std::isfinite(e.end_size) && e.end_size >= 0.0 &&
        std::isfinite(e.emission_remainder) && e.emission_remainder >= 0.0 &&
        e.emission_remainder < 1.0 && e.particles.size() <= e.max_particles;
}
}

double Particle2D::normalized_age() const noexcept {
    if (!std::isfinite(age) || !std::isfinite(lifetime) || lifetime <= 0.0) return 1.0;
    return std::clamp(age / lifetime, 0.0, 1.0);
}
double Particle2D::size() const noexcept {
    const double t = normalized_age();
    return start_size + (end_size - start_size) * t;
}

bool ParticleEmitter2D::update(double delta_seconds, Vec2 origin) {
    if (!std::isfinite(delta_seconds) || delta_seconds < 0.0 || !finite(origin) || !valid(*this)) return false;
    for (const auto& p : particles) {
        if (!finite(p.position) || !finite(p.velocity) || !std::isfinite(p.age) || p.age < 0.0 ||
            !std::isfinite(p.lifetime) || p.lifetime <= 0.0 || !std::isfinite(p.start_size) ||
            !std::isfinite(p.end_size) || p.start_size < 0.0 || p.end_size < 0.0) return false;
    }
    const double accumulated = emission_remainder + (emitting ? particles_per_second * delta_seconds : 0.0);
    if (!std::isfinite(accumulated)) return false;
    const double requested_d = std::floor(accumulated);
    // Compare in long double before conversion: double(size_t::max()) can round
    // up to an unrepresentable integer on 64-bit platforms.
    const auto requested = static_cast<long double>(requested_d) >=
            static_cast<long double>(std::numeric_limits<std::size_t>::max())
        ? max_particles : static_cast<std::size_t>(requested_d);
    const double remainder = accumulated - requested_d;

    auto next = particles;
    for (auto& p : next) {
        p.age += delta_seconds;
        p.position.x += p.velocity.x * delta_seconds;
        p.position.y += p.velocity.y * delta_seconds;
        if (!std::isfinite(p.age) || !finite(p.position)) return false;
    }
    next.erase(std::remove_if(next.begin(), next.end(), [](const Particle2D& p) {
        return p.age >= p.lifetime;
    }), next.end());
    const std::size_t spawn = std::min(requested, max_particles - next.size());
    for (std::size_t i = 0; i < spawn; ++i) {
        const double phase = spawn <= 1 ? 0.0 : static_cast<double>(i) / static_cast<double>(spawn - 1);
        const double signed_phase = phase * 2.0 - 1.0;
        const Vec2 velocity{initial_velocity.x + velocity_spread.x * signed_phase,
                            initial_velocity.y + velocity_spread.y * signed_phase};
        if (!finite(velocity)) return false;
        next.push_back({origin, velocity, 0.0, particle_lifetime, color, start_size, end_size});
    }
    particles.swap(next);
    emission_remainder = remainder;
    return true;
}

void ParticleEmitter2D::clear() noexcept { particles.clear(); emission_remainder = 0.0; }

} // namespace gridengine
