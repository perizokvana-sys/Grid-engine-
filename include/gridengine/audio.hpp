// GridEngine - include/gridengine/audio.hpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gridengine {
enum class PlaybackState : std::uint8_t { Stopped, Playing, Paused };

// Serializable source configuration. Playback state is runtime-only.
struct AudioSource {
    std::filesystem::path resource;
    bool loop{false};
    float volume{1.0F};
    bool muted{false};
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] bool set_volume(float value) noexcept;
    [[nodiscard]] std::string serialize() const;
    [[nodiscard]] static bool deserialize(const std::string& text, AudioSource& out);
};

// Backend-independent lifecycle/state manager; deliberately does not pretend to
// produce sound until a concrete device backend is linked.
class AudioManager final {
public:
    using Handle = std::uint64_t;
    [[nodiscard]] Handle create(const AudioSource& source);
    [[nodiscard]] bool play(Handle handle) noexcept;
    [[nodiscard]] bool pause(Handle handle) noexcept;
    [[nodiscard]] bool stop(Handle handle) noexcept;
    [[nodiscard]] PlaybackState state(Handle handle) const noexcept;
    void remove(Handle handle) noexcept;
    void stop_all() noexcept;
    [[nodiscard]] std::size_t source_count() const noexcept { return entries_.size(); }
private:
    struct Entry { Handle id; AudioSource source; PlaybackState state; };
    std::vector<Entry> entries_;
    Handle next_{1};
};
}
