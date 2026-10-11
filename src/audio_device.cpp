// GridEngine - src/audio_device.cpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#include "gridengine/audio_device.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <system_error>

namespace gridengine {
namespace {

struct SdlDeleter {
    void operator()(SDL_AudioStream* stream) const noexcept {
        if (stream) SDL_DestroyAudioStream(stream);
    }
};

struct WavData {
    SDL_AudioSpec spec{};
    std::vector<std::uint8_t> pcm;
    bool ok{false};
    std::string error;
};

WavData load_wav(const std::filesystem::path& file) {
    WavData data;
    std::error_code ec;
    if (!std::filesystem::exists(file, ec) || ec) {
        data.error = "the audio file does not exist";
        return data;
    }
    const auto extension = file.extension().string();
    if (extension != ".wav" && extension != ".WAV") {
        data.error = "only WAV audio is supported (OGG/MP3 need an external decoder)";
        return data;
    }
    SDL_AudioSpec spec{};
    Uint8* buffer = nullptr;
    Uint32 length = 0;
    if (!SDL_LoadWAV(file.string().c_str(), &spec, &buffer, &length) || !buffer || length == 0) {
        data.error = std::string("cannot decode the WAV: ") + SDL_GetError();
        if (buffer) SDL_free(buffer);
        return data;
    }
    data.spec = spec;
    data.pcm.assign(buffer, buffer + length);
    SDL_free(buffer);
    data.ok = true;
    return data;
}

} // namespace

AudioDevice::~AudioDevice() { shutdown(); }

bool AudioDevice::initialize(std::string& error) {
    shutdown();
    if (!SDL_Init(SDL_INIT_AUDIO)) {
        error = std::string("cannot initialize the audio subsystem: ") + SDL_GetError();
        return false;
    }
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = 44100;
    // Open the device itself (not a device-bound stream): several voice streams
    // must be bindable at once, which SDL_OpenAudioDeviceStream forbids.
    const SDL_AudioDeviceID device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    if (!device) {
        error = std::string("cannot open the playback device: ") + SDL_GetError();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    device_ = device;
    SDL_ResumeAudioDevice(device);
    return true;
}

void AudioDevice::shutdown() noexcept {
    for (auto& voice : voices_) {
        if (voice.stream) {
            SDL_UnbindAudioStream(static_cast<SDL_AudioStream*>(voice.stream));
            SDL_DestroyAudioStream(static_cast<SDL_AudioStream*>(voice.stream));
        }
    }
    voices_.clear();
    if (device_) {
        SDL_CloseAudioDevice(static_cast<SDL_AudioDeviceID>(device_));
        device_ = 0;
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

AudioDevice::Voice AudioDevice::play(const AudioSource& source, const std::filesystem::path& base_root,
                                     Vec2 position, bool spatial, std::string& error) {
    if (!device_) {
        error = "the audio device is not initialized";
        return 0;
    }
    if (!source.valid()) {
        error = "the audio source configuration is invalid";
        return 0;
    }
    const auto resolved = base_root / source.resource;
    const auto wav = load_wav(resolved);
    if (!wav.ok) {
        error = wav.error.empty() ? "the WAV file cannot be decoded" : wav.error;
        const auto message = resolved.string() + ": " + error;
        if (std::find(failed_.begin(), failed_.end(), message) == failed_.end())
            failed_.push_back(message);
        return 0;
    }

    SDL_AudioSpec target{};
    target.format = SDL_AUDIO_S16;
    target.channels = 2;
    target.freq = 44100;

    VoiceData voice;
    voice.source = source;
    voice.position = position;
    voice.spatial = spatial;
    voice.pcm = std::move(wav.pcm);
    SDL_AudioStream* stream = SDL_CreateAudioStream(&wav.spec, &target);
    if (!stream) {
        error = std::string("cannot create the audio stream: ") + SDL_GetError();
        return 0;
    }
    voice.stream = static_cast<void*>(stream);
    if (!SDL_BindAudioStream(static_cast<SDL_AudioDeviceID>(device_), stream)) {
        SDL_DestroyAudioStream(stream);
        error = std::string("cannot bind the audio stream: ") + SDL_GetError();
        return 0;
    }
    if (!SDL_PutAudioStreamData(stream, voice.pcm.data(), static_cast<Uint32>(voice.pcm.size()))) {
        SDL_UnbindAudioStream(stream);
        SDL_DestroyAudioStream(stream);
        error = std::string("cannot queue the audio samples: ") + SDL_GetError();
        return 0;
    }
    SDL_SetAudioStreamGain(stream, static_cast<float>(gain_for(voice)));
    const Voice id = next_++;
    voice.id = id;
    voices_.push_back(std::move(voice));
    bytes_queued_ += static_cast<std::uint64_t>(voices_.back().pcm.size());
    return id;
}

double AudioDevice::gain_for(const VoiceData& voice) const noexcept {
    if (voice.source.muted) return 0.0;
    double gain = std::clamp(voice.source.volume, 0.0F, 1.0F);
    if (!voice.spatial) return gain;
    const double dx = voice.position.x - listener_.position.x;
    const double dy = voice.position.y - listener_.position.y;
    const double distance = std::sqrt(dx * dx + dy * dy);
    if (distance > std::max(listener_.reference_distance, listener_.max_distance)) return 0.0;
    const double reference = listener_.reference_distance > 0.0 ? listener_.reference_distance : 0.0001;
    const double attenuated = reference + listener_.rolloff * std::max(0.0, distance - reference);
    return gain * std::clamp(reference / (attenuated > 0.0 ? attenuated : 1.0), 0.0, 1.0);
}

AudioDevice::VoiceData* AudioDevice::find(Voice voice) noexcept {
    for (auto& entry : voices_)
        if (entry.id == voice) return &entry;
    return nullptr;
}

bool AudioDevice::stop(Voice voice) noexcept {
    auto* entry = find(voice);
    if (!entry || !entry->stream) return false;
    SDL_AudioStream* stream = static_cast<SDL_AudioStream*>(entry->stream);
    SDL_UnbindAudioStream(stream);
    SDL_DestroyAudioStream(stream);
    entry->stream = nullptr;
    entry->finished = true;
    return true;
}

// SDL has no per-stream pause, so pausing silences the voice gain (no output)
// and resuming restores it. The state machine still reports Paused.
bool AudioDevice::pause(Voice voice) noexcept {
    auto* entry = find(voice);
    if (!entry || entry->finished || entry->paused) return false;
    SDL_SetAudioStreamGain(static_cast<SDL_AudioStream*>(entry->stream), 0.0f);
    entry->paused = true;
    return true;
}

bool AudioDevice::resume(Voice voice) noexcept {
    auto* entry = find(voice);
    if (!entry || entry->finished || !entry->paused) return false;
    SDL_SetAudioStreamGain(static_cast<SDL_AudioStream*>(entry->stream),
                           static_cast<float>(gain_for(*entry)));
    entry->paused = false;
    return true;
}

bool AudioDevice::set_volume(Voice voice, float volume) noexcept {
    auto* entry = find(voice);
    if (!entry || !entry->source.set_volume(volume)) return false;
    SDL_SetAudioStreamGain(static_cast<SDL_AudioStream*>(entry->stream),
                           static_cast<float>(gain_for(*entry)));
    return true;
}

PlaybackState AudioDevice::state(Voice voice) const noexcept {
    for (const auto& entry : voices_) {
        if (entry.id != voice) continue;
        if (entry.finished) return PlaybackState::Stopped;
        if (entry.paused) return PlaybackState::Paused;
        return PlaybackState::Playing;
    }
    return PlaybackState::Stopped;
}

void AudioDevice::set_listener(const AudioListenerConfig& listener) noexcept {
    listener_ = listener;
    for (auto& entry : voices_) {
        if (entry.stream) SDL_SetAudioStreamGain(static_cast<SDL_AudioStream*>(entry.stream),
                                                 static_cast<float>(gain_for(entry)));
    }
}

void AudioDevice::stop_all() noexcept {
    for (const auto& voice : voices_)
        if (voice.stream && !voice.finished) stop(voice.id);
}

void AudioDevice::update() noexcept {
    for (auto& entry : voices_) {
        if (!entry.stream || entry.finished || entry.paused) continue;
        SDL_AudioStream* stream = static_cast<SDL_AudioStream*>(entry.stream);
        const int available = SDL_GetAudioStreamAvailable(stream);
        if (available > 0) continue;
        if (entry.source.loop) {
            if (SDL_PutAudioStreamData(stream, entry.pcm.data(), static_cast<Uint32>(entry.pcm.size()))) {
                bytes_queued_ += static_cast<std::uint64_t>(entry.pcm.size());
                SDL_SetAudioStreamGain(stream, static_cast<float>(gain_for(entry)));
            }
            continue;
        }
        // One-shot voice drained: release the stream and mark it finished.
        SDL_UnbindAudioStream(stream);
        SDL_DestroyAudioStream(stream);
        entry.stream = nullptr;
        entry.finished = true;
    }
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                 [](const VoiceData& v) { return v.finished && !v.stream; }),
                  voices_.end());
}

} // namespace gridengine
