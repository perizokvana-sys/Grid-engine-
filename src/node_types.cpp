// GridEngine - src/node_types.cpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#include "gridengine/node_types.hpp"

#include <array>

namespace gridengine {
namespace {

// The catalog in menu order. Every entry from the reference art is present:
// Core, Visual, Logic, Input and Physics. `summary` explains what the node is,
// so the menu can show a tooltip without any extra data source.
constexpr std::array<NodeTypeInfo, 30> kCatalog{{
    // Core
    {NodeKind::node2d, NodeCategory::core, "Node2D", "Base node: a position in the scene"},
    {NodeKind::canvas_layer, NodeCategory::core, "CanvasLayer",
     "Draw order layer (no renderer layer support yet)"},
    {NodeKind::y_sort, NodeCategory::core, "YSort", "Depth sorting by Y position (not implemented yet)"},
    // Visual
    {NodeKind::sprite2d, NodeCategory::visual, "Sprite2D", "A textured quad with tint and size"},
    {NodeKind::animated_sprite2d, NodeCategory::visual, "AnimatedSprite2D",
     "Sprite frames (frame playback not implemented yet)"},
    {NodeKind::texture_rect, NodeCategory::visual, "TextureRect", "A rectangle filled with a texture"},
    {NodeKind::tile_map, NodeCategory::visual, "TileMap", "The scene tile map (scene level)"},
    {NodeKind::tile_map_layer, NodeCategory::visual, "TileMapLayer", "A single layer of the tile map"},
    {NodeKind::line2d, NodeCategory::visual, "Line2D", "A polyline (no line renderer yet)"},
    {NodeKind::polygon2d, NodeCategory::visual, "Polygon2D", "A filled polygon (no polygon renderer yet)"},
    {NodeKind::label, NodeCategory::visual, "Label", "Text (no font renderer yet)"},
    {NodeKind::rich_text_label, NodeCategory::visual, "RichTextLabel", "Styled text (no font renderer yet)"},
    {NodeKind::button, NodeCategory::visual, "Button", "Clickable UI element (no UI system yet)"},
    {NodeKind::panel, NodeCategory::visual, "Panel", "Background rectangle (no UI system yet)"},
    {NodeKind::color_rect, NodeCategory::visual, "ColorRect", "A solid colour rectangle"},
    {NodeKind::texture_button, NodeCategory::visual, "TextureButton",
     "Textured button (no UI system yet)"},
    // Logic
    {NodeKind::timer, NodeCategory::logic, "Timer",
     "Counts down and fires again (checked by the play loop)"},
    {NodeKind::animation_player, NodeCategory::logic, "AnimationPlayer",
     "Plays animations (not implemented yet)"},
    {NodeKind::tween, NodeCategory::logic, "Tween", "Interpolates values (not implemented yet)"},
    {NodeKind::state_machine, NodeCategory::logic, "StateMachine",
     "State transitions (not implemented yet)"},
    {NodeKind::signal_bus, NodeCategory::logic, "SignalBus",
     "Routes Grid events (not implemented yet)"},
    {NodeKind::audio_stream_player2d, NodeCategory::logic, "AudioStreamPlayer2D",
     "Plays a WAV with 2D volume and loop"},
    // Input
    {NodeKind::input_handler, NodeCategory::input, "InputHandler",
     "Collects input state (not implemented yet)"},
    {NodeKind::keybind, NodeCategory::input, "KeyBind", "Key bindings (not implemented yet)"},
    {NodeKind::touch_screen_button, NodeCategory::input, "TouchScreenButton",
     "Touch control (not implemented yet)"},
    // Physics
    {NodeKind::collision_shape2d, NodeCategory::physics, "CollisionShape2D",
     "An axis-aligned collider rectangle"},
    {NodeKind::area2d, NodeCategory::physics, "Area2D", "A volume that detects overlaps"},
    {NodeKind::rigid_body2d, NodeCategory::physics, "RigidBody2D",
     "Moves by velocity, collides with the scene"},
    {NodeKind::character_body2d, NodeCategory::physics, "CharacterBody2D",
     "Kinematic movement with a collider"},
    {NodeKind::static_body2d, NodeCategory::physics, "StaticBody2D",
     "An immovable collider"},
}};

} // namespace

const std::vector<NodeTypeInfo>& node_catalog() {
    static const std::vector<NodeTypeInfo> catalog{kCatalog.begin(), kCatalog.end()};
    return catalog;
}

std::size_t node_catalog_size() { return kCatalog.size(); }

std::vector<NodeTypeInfo> node_catalog_of(NodeCategory category) {
    std::vector<NodeTypeInfo> out;
    for (const auto& entry : node_catalog())
        if (entry.category == category) out.push_back(entry);
    return out;
}

const NodeTypeInfo* node_type_info(NodeKind kind) {
    for (const auto& entry : node_catalog())
        if (entry.kind == kind) return &entry;
    return nullptr;
}

std::string_view node_category_name(NodeCategory category) {
    switch (category) {
        case NodeCategory::core: return "Core";
        case NodeCategory::visual: return "Visual";
        case NodeCategory::logic: return "Logic";
        case NodeCategory::input: return "Input";
        case NodeCategory::physics: return "Physics";
        case NodeCategory::custom_script: return "Custom";
    }
    return "Core";
}

NodePlacement node_placement(NodeKind kind) {
    NodePlacement placement;
    placement.kind = kind;
    auto named = [&placement](const char* name, const char* note = nullptr) {
        placement.name = name;
        if (note) placement.note = note;
    };

    switch (kind) {
        case NodeKind::node2d:
            named("Node2D");
            break;
        case NodeKind::canvas_layer:
            named("CanvasLayer", "CanvasLayer has no renderer layer support yet; the node is an empty group");
            break;
        case NodeKind::y_sort:
            named("YSort", "YSort depth sorting is not implemented yet; the node is an empty group");
            break;
        case NodeKind::sprite2d:
            placement.sprite = true;
            named("Sprite2D");
            break;
        case NodeKind::animated_sprite2d:
            placement.sprite = true;
            named("AnimatedSprite2D", "AnimatedSprite2D has a sprite but no frame playback yet");
            break;
        case NodeKind::texture_rect:
            placement.sprite = true;
            named("TextureRect");
            break;
        case NodeKind::tile_map:
            placement.tile_map = true;
            named("TileMap", "TileMap is scene level: paint with the paint tool in the Scene view");
            break;
        case NodeKind::tile_map_layer:
            placement.tile_map = true;
            named("TileMapLayer", "the tile map has a single layer, so layers are not separate nodes");
            break;
        case NodeKind::line2d:
            named("Line2D", "Line2D has no line renderer yet; the node is an empty group");
            break;
        case NodeKind::polygon2d:
            named("Polygon2D", "Polygon2D has no polygon renderer yet; the node is an empty group");
            break;
        case NodeKind::label:
            named("Label", "Label has no font renderer yet; the node is an empty group");
            break;
        case NodeKind::rich_text_label:
            named("RichTextLabel", "RichTextLabel has no font renderer yet; the node is an empty group");
            break;
        case NodeKind::button:
            named("Button", "Button has no UI system yet; the node is an empty group");
            break;
        case NodeKind::panel:
            named("Panel", "Panel has no UI system yet; the node is an empty group");
            break;
        case NodeKind::color_rect:
            placement.sprite = true;
            named("ColorRect", "ColorRect renders as a solid tinted quad; there is no UI layout yet");
            break;
        case NodeKind::texture_button:
            placement.sprite = true;
            named("TextureButton", "TextureButton shows a sprite but has no UI click handling yet");
            break;
        case NodeKind::timer:
            named("Timer", "Timer only counts while Play is running and reports through the console");
            break;
        case NodeKind::animation_player:
            named("AnimationPlayer", "AnimationPlayer has no animation data yet; the node is an empty group");
            break;
        case NodeKind::tween:
            named("Tween", "Tween has no interpolation runtime yet; the node is an empty group");
            break;
        case NodeKind::state_machine:
            named("StateMachine", "StateMachine has no state table yet; the node is an empty group");
            break;
        case NodeKind::signal_bus:
            named("SignalBus", "SignalBus has no routing yet; the node is an empty group");
            break;
        case NodeKind::audio_stream_player2d:
            placement.audio = true;
            named("AudioStreamPlayer2D", "AudioStreamPlayer2D plays a WAV from assets/ (set the file in the Inspector)");
            break;
        case NodeKind::input_handler:
            named("InputHandler", "InputHandler has no input sampling yet; the node is an empty group");
            break;
        case NodeKind::keybind:
            named("KeyBind", "KeyBind has no binding table yet; the node is an empty group");
            break;
        case NodeKind::touch_screen_button:
            named("TouchScreenButton", "TouchScreenButton has no touch input yet; the node is an empty group");
            break;
        case NodeKind::collision_shape2d:
            placement.collider = true;
            named("CollisionShape2D");
            break;
        case NodeKind::area2d:
            placement.collider = true;
            named("Area2D", "Area2D detects overlaps through the scene collision query");
            break;
        case NodeKind::rigid_body2d:
            placement.motion = true;
            placement.collider = true;
            named("RigidBody2D", "RigidBody2D integrates velocity every step; it is not a full solver");
            break;
        case NodeKind::character_body2d:
            placement.motion = true;
            placement.collider = true;
            named("CharacterBody2D");
            break;
        case NodeKind::static_body2d:
            placement.collider = true;
            named("StaticBody2D");
            break;
        case NodeKind::custom:
            placement.script = true;
            named("CustomNode");
            break;
    }
    return placement;
}

} // namespace gridengine
