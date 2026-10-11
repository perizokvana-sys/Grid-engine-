// GridEngine - src/audio.cpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#include "gridengine/audio.hpp"
#include <cmath>
#include <iomanip>
#include <sstream>

namespace gridengine {
bool AudioSource::valid() const noexcept {
    if (resource.empty() || !std::isfinite(volume) || volume < 0.0F || volume > 1.0F) return false;
    const auto ext = resource.extension().string();
    return ext == ".wav" || ext == ".WAV" || ext == ".ogg" || ext == ".OGG";
}
bool AudioSource::set_volume(float value) noexcept {
    if (!std::isfinite(value) || value < 0.0F || value > 1.0F) return false;
    volume = value; return true;
}
std::string AudioSource::serialize() const {
    std::ostringstream out;
    out << "GridAudioSource 1\n" << std::quoted(resource.generic_string()) << '\n'
        << loop << ' ' << std::setprecision(9) << volume << ' ' << muted << '\n';
    return out.str();
}
bool AudioSource::deserialize(const std::string& text, AudioSource& out) {
    std::istringstream in(text); std::string magic, path; int version = 0, loop = 0, muted = 0; float volume = 0;
    if (!(in >> magic >> version) || magic != "GridAudioSource" || version != 1 ||
        !(in >> std::quoted(path)) || !(in >> loop >> volume >> muted) ||
        (loop != 0 && loop != 1) || (muted != 0 && muted != 1)) return false;
    in >> std::ws; if (!in.eof()) return false;
    AudioSource candidate{std::filesystem::path(path), loop != 0, volume, muted != 0};
    if (!candidate.valid()) return false;
    out = std::move(candidate); return true;
}
AudioManager::Handle AudioManager::create(const AudioSource& source) {
    if (!source.valid() || next_ == 0) return 0;
    const auto id = next_++; entries_.push_back({id, source, PlaybackState::Stopped}); return id;
}
bool AudioManager::play(Handle id) noexcept { for (auto& e : entries_) if (e.id == id) { e.state = PlaybackState::Playing; return true; } return false; }
bool AudioManager::pause(Handle id) noexcept { for (auto& e : entries_) if (e.id == id && e.state == PlaybackState::Playing) { e.state = PlaybackState::Paused; return true; } return false; }
bool AudioManager::stop(Handle id) noexcept { for (auto& e : entries_) if (e.id == id) { e.state = PlaybackState::Stopped; return true; } return false; }
PlaybackState AudioManager::state(Handle id) const noexcept { for (const auto& e : entries_) if (e.id == id) return e.state; return PlaybackState::Stopped; }
void AudioManager::remove(Handle id) noexcept { for (auto it = entries_.begin(); it != entries_.end(); ++it) if (it->id == id) { it->state = PlaybackState::Stopped; entries_.erase(it); return; } }
void AudioManager::stop_all() noexcept { for (auto& e : entries_) e.state = PlaybackState::Stopped; }
}
