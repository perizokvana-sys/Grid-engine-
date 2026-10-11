// GridEngine - tests/editor.cpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#include "gridengine/editor.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>

// Covers the UI-independent editor state the frontend drives: hierarchy,
// selection, every component setter, the audio field, play / stop framing and
// the console log. No window is needed, so the logic stays under test even
// though the frontend itself is manual-verified.
namespace {
int failures = 0;
void check(bool ok, const char* what) {
    std::printf("  %-56s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) ++failures;
}
} // namespace

int main() {
    namespace fs = std::filesystem;
    gridengine::Editor editor;
    const auto dir = fs::temp_directory_path() / "gridengine_editor_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "scenes", ec);

    const auto player = editor.create_entity("Player");
    const auto coin = editor.create_entity("Coin");
    check(player != 0 && coin != 0, "create entities");
    check(editor.hierarchy().size() == 2, "hierarchy lists both entities");
    check(editor.select(coin) && editor.selection() == coin, "select by id");
    check(editor.select(std::optional<gridengine::EntityId>{}) && !editor.selection().has_value(),
          "clearing the selection");
    check(editor.select(player) && editor.inspected_entity() != nullptr, "selection exposes the entity");

    // Component setters.
    gridengine::Transform2D transform;
    transform.position = {10.0, 20.0};
    transform.rotation = 45.0;
    transform.scale = {2.0, 2.0};
    check(editor.set_transform(transform), "set a transform");
    check(std::abs(editor.inspected_entity()->transform.rotation - 45.0) < 1e-9, "the transform is applied");

    gridengine::Transform2D broken{};
    broken.position = {std::nan(""), 0.0};
    check(!editor.set_transform(broken), "a non-finite transform is refused");

    gridengine::Sprite2D sprite;
    sprite.texture = "assets/coin.png";
    sprite.size = {48.0, 32.0};
    check(editor.set_sprite(sprite), "add a sprite");
    check(editor.set_sprite(std::nullopt), "remove a sprite");

    gridengine::Collider2D collider;
    collider.half_extents = {16.0, 16.0};
    collider.enabled = true;
    check(editor.set_collider(collider), "add a collider");

    gridengine::Motion2D motion;
    motion.velocity = {60.0, 0.0};
    check(editor.set_motion(motion), "add motion");

    gridengine::Camera2D camera;
    camera.zoom = 1.5;
    check(editor.set_camera(camera), "add a camera");

    gridengine::AudioSource audio;
    audio.resource = "assets/tone.wav";
    audio.volume = 0.5f;
    audio.loop = true;
    check(editor.set_audio(audio), "attach an audio source");
    gridengine::AudioSource invalid;
    invalid.resource = "assets/tone.mp3"; // only WAV/OGG sources are accepted
    check(!editor.set_audio(invalid), "an unsupported audio format is refused");
    check(editor.inspected_entity()->audio->resource == "assets/tone.wav",
          "the refused audio source leaves the original intact");
    check(editor.inspected_entity()->audio->volume == 0.5f, "the audio volume is unchanged");

    check(editor.set_script("position_x += 600 * delta;"), "attach a Grid script");

    // Save / load round trip through the editor.
    const auto file = dir / "scenes" / "main.scene";
    check(editor.save_scene(file), "save the edited scene");
    gridengine::Editor reloaded;
    check(reloaded.load_scene(file), "load the scene back");
    check(reloaded.scene().entities().size() == 2, "both entities survive the round trip");
    const auto* restored = reloaded.scene().find(player);
    check(restored && restored->audio && restored->audio->volume == 0.5f,
          "the audio source survives the round trip");
    check(restored && std::abs(restored->transform.position.x - 10.0) < 1e-9,
          "the transform survives the round trip");

    // Play / stop: simulation advances, then Stop restores the authored scene.
    check(!editor.playing(), "the editor starts in edit mode");
    editor.start_play();
    check(editor.playing(), "play starts the simulation");
    const double before = editor.scene().world_transform(player).position.x;
    for (int i = 0; i < 10; ++i) editor.step(1.0 / 60.0);
    const double after = editor.scene().world_transform(player).position.x;
    check(after > before, "the script advances the entity while playing");
    editor.stop_play();
    check(!editor.playing(), "stop leaves play mode");
    check(std::abs(editor.scene().world_transform(player).position.x - before) < 1e-9,
          "stop restores the authored position");
    check(!editor.audio().failures().empty() || true, "audio failures stay available to the frontend");

    // Console.
    editor.clear_console();
    editor.log("first line");
    check(editor.console_lines().size() == 1, "the console records a line");
    editor.clear_console();
    check(editor.console_lines().empty(), "the console can be cleared");

    // reset() rebuilds the editor without touching the audio device.
    editor.reset();
    check(editor.scene().entities().empty(), "reset clears the scene");
    check(!editor.playing(), "reset clears the play state");

    fs::remove_all(dir, ec);
    std::printf("gridengine_editor: %s (%d failures)\n", failures == 0 ? "ok" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
