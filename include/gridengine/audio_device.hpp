// GridEngine - include/gridengine/audio_device.hpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#pragma once
#include "gridengine/audio.hpp"
#include "gridengine/scene.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Real audio playback on top of SDL3 audio streams.
//
// Every voice is an SDL_AudioStream bound to one device stream, so SDL performs
// the mixing and sample conversion. Volume and spatial attenuation map onto
// SDL's per-stream gain. WAV files are decoded by SDL; anything else is
// rejected with a clear error. Playback works with the dummy audio driver too
// (SDL_AUDIODRIVER=dummy), which is what the automated test uses.

namespace gridengine {

struct AudioListenerConfig {
    Vec2 position{};
    double reference_distance{100.0};
    double max_distance{2000.0};
    double rolloff{1.0};
};

class AudioDevice final {
public:
    using Voice = std::uint64_t;
    AudioDevice() = default;
    ~AudioDevice();
    AudioDevice(const AudioDevice&) = delete;
    AudioDevice& operator=(const AudioDevice&) = delete;

    // Opens the default playback device (dummy without a sound card).
    [[nodiscard]] bool initialize(std::string& error);
    void shutdown() noexcept;

    // Starts playback of `source` located at `position`. With `spatial` the
    // volume follows the listener distance; otherwise it is a plain 2D voice.
    // Returns 0 with a human-readable `error` when the file cannot be played.
    [[nodiscard]] Voice play(const AudioSource& source, const std::filesystem::path& base_root,
                             Vec2 position, bool spatial, std::string& error);
    [[nodiscard]] bool stop(Voice voice) noexcept;
    [[nodiscard]] bool pause(Voice voice) noexcept;
    [[nodiscard]] bool resume(Voice voice) noexcept;
    [[nodiscard]] bool set_volume(Voice voice, float volume) noexcept;
    [[nodiscard]] PlaybackState state(Voice voice) const noexcept;
    void set_listener(const AudioListenerConfig& listener) noexcept;
    void update() noexcept;
    void     stop_all() noexcept;
    [[nodiscard]] std::size_t voice_count() const noexcept { return voices_.size(); }
    [[nodiscard]] std::size_t failed_count() const noexcept { return failed_.size(); }
    [[nodiscard]] const std::vector<std::string>& failures() const noexcept { return failed_; }
    // Diagnostics for tests: how much PCM the device actually consumed.
    [[nodiscard]] std::uint64_t bytes_queued() const noexcept { return bytes_queued_; }

private:
    struct VoiceData {
        Voice id{0};
        std::vector<std::uint8_t> pcm;
        void* stream{nullptr}; // SDL_AudioStream*, kept opaque in the header
        AudioSource source;
        Vec2 position{};
        bool spatial{false};
        bool paused{false};
        bool finished{false};
        std::string label;
    };
    [[nodiscard]] VoiceData* find(Voice voice) noexcept;
    [[nodiscard]] double gain_for(const VoiceData& voice) const noexcept;

    std::vector<VoiceData> voices_;
    std::vector<std::string> failed_;
    AudioListenerConfig listener_{};
    std::uint64_t device_{0}; // SDL_AudioDeviceID (Uint32)
    std::uint64_t next_{1};
    std::uint64_t bytes_queued_{0};
};

} // namespace gridengine
