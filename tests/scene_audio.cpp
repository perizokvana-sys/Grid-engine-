// GridEngine - tests/scene_audio.cpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#include "gridengine/scene.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

// Round-trip of the audio source through the GridScene 6 text format, plus
// rejection of malformed audio data and backward compatibility with 1-5.
namespace {
int failures = 0;
void check(bool ok, const char* what) {
    std::printf("  %-56s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) ++failures;
}
} // namespace

int main() {
    using gridengine::Scene;
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() / "gridengine_scene_audio_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "scenes", ec);

    Scene scene;
    const auto player = scene.create_entity("Player");
    const auto coin = scene.create_entity("Coin");
    if (player) scene.find(player)->transform.position = {100.0, 100.0};
    if (coin) scene.find(coin)->transform.position = {200.0, 200.0};

    gridengine::AudioSource source;
    source.resource = "assets/tone.wav";
    source.volume = 0.75F;
    source.loop = true;
    if (player) scene.find(player)->audio = source;

    const auto file = dir / "scenes" / "main.scene";
    check(scene.save(file), "save a scene with an audio source");

    const auto loaded = Scene::load(file);
    check(loaded.has_value(), "load the scene back");
    if (loaded) {
        const auto* e = loaded->find(player);
        check(e && e->audio.has_value(), "the audio source survived the round trip");
        if (e && e->audio) {
            check(e->audio->resource == source.resource, "the audio resource path is identical");
            check(e->audio->volume == source.volume, "the audio volume is identical");
            check(e->audio->loop == source.loop, "the audio loop flag is identical");
            check(e->audio->muted == source.muted, "the audio mute flag is identical");
        }
        check(loaded->find(coin) && !loaded->find(coin)->audio.has_value(),
              "entities without audio keep no audio source");
        // The audio field must not disturb the script round trip.
        check(loaded->find(coin) != nullptr, "the second entity survives the round trip");
    }

    // Malformed audio data must be rejected instead of silently ignored.
    {
        std::string text = "GridScene 6\n"
            "E 1 0 \"Player\" 0 0 0 1 1 0 0 0 0 \"\" 1 \"assets/tone.wav\" 0.8 0 1\n";
        const auto bad = dir / "scenes" / "bad_volume.scene";
        { std::ofstream out(bad, std::ios::binary); out << text; }
        check(Scene::load(bad).has_value(), "a fully specified audio source loads");
    }
    {
        const auto truncated = dir / "scenes" / "truncated.scene";
        { std::ofstream out(truncated, std::ios::binary);
          out << "GridScene 6\nE 1 0 \"Player\" 0 0 0 1 1 0 0 1 \"\" 0\n"; }
        check(!Scene::load(truncated).has_value(), "a truncated audio record is rejected");
    }
    {
        const auto bad_volume = dir / "scenes" / "bad_volume2.scene";
        { std::ofstream out(bad_volume, std::ios::binary);
          out << "GridScene 6\nE 1 0 \"Player\" 0 0 0 1 1 0 0 1 \"\" 0 \"\" 1 \"assets/tone.wav\" notanumber 0 1 \"\"\n"; }
        check(!Scene::load(bad_volume).has_value(), "a non-numeric audio volume is rejected");
    }
    // Older formats without audio still load.
    {
        const auto legacy = dir / "scenes" / "legacy.scene";
        { std::ofstream out(legacy, std::ios::binary);
          out << "GridScene 5\nE 1 0 \"Hero\" 80 340 0 1 1 0 0 1 \"hero.png\" 48 48 1 4294967295 1 1 1 \"\"\n"; }
        const auto old = Scene::load(legacy);
        check(old.has_value(), "a format 5 scene loads without audio support");
        check(old && old->find(1) && !old->find(1)->audio.has_value(), "legacy entities carry no audio source");
    }

    fs::remove_all(dir, ec);
    std::printf("gridengine_scene_audio: %s (%d failures)\n", failures == 0 ? "ok" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
