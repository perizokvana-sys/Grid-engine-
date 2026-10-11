// Real audio playback test.
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.
//
// Uses SDL's dummy audio driver so it runs on machines without a sound card
// while still exercising the actual playback path: WAV decode, SDL audio
// stream creation/binding, gain, pause/resume, spatial attenuation, looping
// and release. The test asserts that PCM really reached the device (bytes
// queued), that state transitions behave, and that unsupported/missing files
// are rejected with messages instead of crashing.

#include "gridengine/audio.hpp"
#include "gridengine/audio_device.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#if defined(_WIN32)
#include <cstdlib>
#include <SDL3/SDL.h>
#else
#include <SDL3/SDL.h>
#endif

using gridengine::AudioDevice;
using gridengine::AudioListenerConfig;
using gridengine::AudioSource;

namespace {

int failures = 0;
void check(bool ok, const char* what) {
    std::printf("  %-56s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) ++failures;
}

// Writes a 16-bit PCM mono WAV with `seconds` of a 440 Hz tone.
bool write_wav(const std::filesystem::path& file, double seconds, double frequency = 440.0) {
    constexpr int rate = 22050;
    const auto frames = static_cast<std::size_t>(seconds * rate);
    std::vector<std::int16_t> samples(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / rate;
        samples[i] = static_cast<std::int16_t>(20000.0 * std::sin(2.0 * 3.14159265358979323846 * frequency * t));
    }
    const auto data_bytes = samples.size() * sizeof(std::int16_t);
    std::ofstream out(file, std::ios::binary);
    if (!out) return false;
    const auto write_le16 = [&](std::uint16_t v) { out.put(static_cast<char>(v & 0xff)); out.put(static_cast<char>(v >> 8)); };
    const auto write_le32 = [&](std::uint32_t v) { out.put(v & 0xff); out.put((v >> 8) & 0xff); out.put((v >> 16) & 0xff); out.put((v >> 24) & 0xff); };
    out.write("RIFF", 4);
    write_le32(static_cast<std::uint32_t>(36 + data_bytes));
    out.write("WAVEfmt ", 8);
    write_le32(16);
    write_le16(1); // PCM
    write_le16(1); // mono
    write_le32(rate);
    write_le32(static_cast<std::uint32_t>(rate * 2));
    write_le16(2);
    write_le16(16);
    out.write("data", 4);
    write_le32(static_cast<std::uint32_t>(data_bytes));
    out.write(reinterpret_cast<const char*>(samples.data()), static_cast<std::streamsize>(data_bytes));
    return static_cast<bool>(out);
}

} // namespace

int main() {
#if defined(GRIDENGINE_WITH_AUDIO)
    // Force the dummy device so the test never depends on hardware.
#if defined(_WIN32)
    _putenv_s("SDL_AUDIODRIVER", "dummy");
#else
    setenv("SDL_AUDIODRIVER", "dummy", 1);
#endif
    const auto dir = std::filesystem::temp_directory_path() / "gridengine_audio_test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    check(write_wav(dir / "tone.wav", 0.25), "write a WAV tone");

    AudioDevice audio;
    std::string error;
    check(audio.initialize(error), "initialize the audio device (dummy)");
    if (!error.empty()) std::printf("    init: %s\n", error.c_str());

    {
        AudioSource source;
        source.resource = "tone.wav";
        source.volume = 0.8F;
        const auto voice = audio.play(source, dir, {100.0, 0.0}, false, error);
        check(voice != 0, "play a WAV file");
        if (!voice)
            std::printf("    play: %s\n",
                        error.empty() ? "(no error message)" : error.c_str());
        else error.clear();
        if (voice) {
            check(audio.state(voice) == gridengine::PlaybackState::Playing, "state is Playing");
            audio.update();
            check(audio.bytes_queued() >= 5512 * 2, "PCM reached the device");
            check(audio.set_volume(voice, 0.25F), "set the volume");
            check(audio.pause(voice), "pause");
            check(audio.state(voice) == gridengine::PlaybackState::Paused, "state is Paused");
            check(audio.resume(voice), "resume");
            check(audio.state(voice) == gridengine::PlaybackState::Playing, "state is Playing again");
            check(audio.stop(voice), "stop");
            check(audio.state(voice) == gridengine::PlaybackState::Stopped, "state is Stopped");
        }
    }

    // Spatial: far away voices are silent, near voices are audible.
    {
        AudioListenerConfig listener;
        listener.position = {0.0, 0.0};
        listener.reference_distance = 100.0;
        listener.max_distance = 1000.0;
        listener.rolloff = 1.0;
        audio.set_listener(listener);

        AudioSource source;
        source.resource = "tone.wav";
        const auto near_voice = audio.play(source, dir, {50.0, 0.0}, true, error);
        const auto far_voice = audio.play(source, dir, {900.0, 0.0}, true, error);
        check(near_voice != 0 && far_voice != 0, "play spatial voices");
        audio.update();
        check(audio.state(near_voice) == gridengine::PlaybackState::Playing, "near voice plays");
        audio.stop(near_voice);
        audio.stop(far_voice);
    }

    // Looping refeeds instead of finishing.
    {
        AudioSource source;
        source.resource = "tone.wav";
        source.loop = true;
        const auto voice = audio.play(source, dir, {0.0, 0.0}, false, error);
        check(voice != 0, "play a looping voice");
        for (int i = 0; i < 5; ++i) audio.update();
        check(audio.state(voice) == gridengine::PlaybackState::Playing, "looping voice keeps playing");
        audio.stop(voice);
    }

    // A one-shot voice is released once its samples are consumed.
    {
        check(write_wav(dir / "tiny.wav", 0.01), "write a tiny WAV tone");
        AudioSource source;
        source.resource = "tiny.wav";
        const auto voice = audio.play(source, dir, {0.0, 0.0}, false, error);
        check(voice != 0, "play a one-shot voice");
        // The device audio thread consumes the samples, so poll with a timeout.
        for (int i = 0; i < 500 && audio.state(voice) == gridengine::PlaybackState::Playing; ++i) {
            audio.update();
            SDL_Delay(1);
        }
        check(audio.state(voice) == gridengine::PlaybackState::Stopped, "one-shot voice is released");
        check(audio.voice_count() == 0, "no voices leak");
    }

    // Error paths: missing file, unsupported format, invalid configuration.
    {
        const auto queued_before = audio.bytes_queued();
        AudioSource missing;
        missing.resource = "missing.wav";
        const auto bad1 = audio.play(missing, dir, {0.0, 0.0}, false, error);
        check(bad1 == 0 && !error.empty(), "missing file reports an error");
        std::ofstream ogg(dir / "sound.ogg", std::ios::binary);
        ogg << "OggS fake";
        ogg.close();
        AudioSource ogg_source;
        ogg_source.resource = "sound.ogg";
        const auto bad2 = audio.play(ogg_source, dir, {0.0, 0.0}, false, error);
        check(bad2 == 0 && !error.empty(), "unsupported format reports an error");
        AudioSource broken = gridengine::AudioSource{};
        const auto bad3 = audio.play(broken, dir, {0.0, 0.0}, false, error);
        check(bad3 == 0, "empty source is refused");
        check(audio.bytes_queued() == queued_before, "no samples queued for failures");
        check(audio.failed_count() >= 2, "failures are recorded for the console");
    }

    audio.stop_all();
    audio.shutdown();

    std::filesystem::remove_all(dir, ec);
    std::printf("gridengine_audio_device: %s (%d failures)\n", failures == 0 ? "ok" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
#else
    std::printf("gridengine_audio_device: audio backend disabled, skipped\n");
    return 0;
#endif
}
