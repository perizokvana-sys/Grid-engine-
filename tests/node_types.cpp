// GridEngine - tests/node_types.cpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#include "gridengine/editor.hpp"
#include "gridengine/node_types.hpp"

#include <cstdio>
#include <filesystem>
#include <string>

// The node catalog must contain every node type of the reference art, keep the
// category order, and each node must attach the components it promises.
namespace {
int failures = 0;
void check(bool ok, const char* what) {
    std::printf("  %-58s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) ++failures;
}
std::size_t count_of(gridengine::NodeCategory category) {
    return gridengine::node_catalog_of(category).size();
}
bool has_node(const char* name) {
    for (const auto& entry : gridengine::node_catalog())
        if (entry.name == name) return true;
    return false;
}
} // namespace

int main() {
    const auto& catalog = gridengine::node_catalog();
    check(catalog.size() == 30, "the catalog lists 30 node types");
    check(count_of(gridengine::NodeCategory::core) == 3, "Core: Node2D, CanvasLayer, YSort");
    check(count_of(gridengine::NodeCategory::visual) == 13, "Visual: 13 node types");
    check(count_of(gridengine::NodeCategory::logic) == 6, "Logic: 6 node types");
    check(count_of(gridengine::NodeCategory::input) == 3, "Input: 3 node types");
    check(count_of(gridengine::NodeCategory::physics) == 5, "Physics: 5 node types");

    // Every node from the reference must be present.
    const char* expected[] = {
        "Node2D", "CanvasLayer", "YSort",
        "Sprite2D", "AnimatedSprite2D", "TextureRect", "TileMap", "TileMapLayer", "Line2D",
        "Polygon2D", "Label", "RichTextLabel", "Button", "Panel", "ColorRect", "TextureButton",
        "Timer", "AnimationPlayer", "Tween", "StateMachine", "SignalBus", "AudioStreamPlayer2D",
        "InputHandler", "KeyBind", "TouchScreenButton",
        "CollisionShape2D", "Area2D", "RigidBody2D", "CharacterBody2D", "StaticBody2D"};
    bool all = true;
    for (const char* name : expected)
        if (!has_node(name)) { all = false; std::printf("    missing: %s\n", name); }
    check(all, "every node of the reference art exists");
    check(gridengine::node_type_info(gridengine::NodeKind::sprite2d) != nullptr,
          "nodes are found by kind");
    check(gridengine::node_category_name(gridengine::NodeCategory::visual) == "Visual",
          "category names");

    // Placements: what the engine really attaches.
    const auto sprite = gridengine::node_placement(gridengine::NodeKind::sprite2d);
    check(sprite.sprite && sprite.name == "Sprite2D", "Sprite2D attaches a sprite");
    const auto body = gridengine::node_placement(gridengine::NodeKind::character_body2d);
    check(body.motion && body.collider, "CharacterBody2D attaches motion and a collider");
    const auto shape = gridengine::node_placement(gridengine::NodeKind::collision_shape2d);
    check(shape.collider && !shape.motion, "CollisionShape2D attaches only a collider");
    const auto audio = gridengine::node_placement(gridengine::NodeKind::audio_stream_player2d);
    check(audio.audio, "AudioStreamPlayer2D attaches an audio source");
    const auto line = gridengine::node_placement(gridengine::NodeKind::line2d);
    check(!line.sprite && !line.note.empty(), "Line2D honestly reports that it has no renderer");

    // Creating nodes in the editor attaches the mapped components.
    const auto dir = std::filesystem::temp_directory_path() / "gridengine_node_types_test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    gridengine::Editor editor;
    const auto player = editor.add_node(gridengine::NodeKind::sprite2d);
    check(player != 0, "a Sprite2D node is created");
    check(editor.scene().find(player)->sprite.has_value(), "the new entity has a sprite");
    check(editor.created_node(player) == gridengine::NodeKind::sprite2d, "the node kind is remembered");
    const auto enemy = editor.add_node(gridengine::NodeKind::rigid_body2d);
    const auto* enemy_entity = editor.scene().find(enemy);
    check(enemy_entity && enemy_entity->motion && enemy_entity->collider,
          "a RigidBody2D node gets motion and a collider");
    const auto area = editor.add_node(gridengine::NodeKind::area2d);
    check(editor.scene().find(area)->collider.has_value(), "an Area2D node gets a collider");

    const auto timer = editor.add_node(gridengine::NodeKind::timer);
    check(timer != 0, "a Timer node is created");
    const auto lines_before = editor.console_lines().size();
    editor.start_play();
    for (int i = 0; i < 3; ++i) editor.step(1.0);
    check(editor.console_lines().size() > lines_before, "the Timer fires while playing");
    editor.stop_play();
    const auto lines_after_stop = editor.console_lines().size();
    editor.step(1.0);
    check(editor.console_lines().size() == lines_after_stop, "the Timer stops with play mode");

    const auto script = editor.add_script_node("MyCustomNode", "position_x += 10 * delta;");
    check(script != 0, "a custom Grid node is created");
    check(editor.scene().find(script)->name == "MyCustomNode", "the custom node keeps its name");
    check(editor.scene().find(script)->script == "position_x += 10 * delta;",
          "the custom node carries the Grid script");
    check(editor.created_node(script) == gridengine::NodeKind::custom,
          "the custom node is remembered");

    // Nodes round trip through the scene file (components are persisted).
    const auto file = dir / "scenes" / "main.scene";
    std::filesystem::create_directories(dir / "scenes", ec);
    check(editor.save_scene(file), "the scene with new nodes saves");
    gridengine::Editor reloaded;
    check(reloaded.load_scene(file), "the scene reloads");
    check(reloaded.scene().entities().size() >= 4, "the created nodes survive the round trip");

    std::filesystem::remove_all(dir, ec);
    std::printf("gridengine_node_types: %s (%d failures)\n", failures == 0 ? "ok" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
