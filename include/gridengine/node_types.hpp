// GridEngine - include/gridengine/node_types.hpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#pragma once

#include "gridengine/scene.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace gridengine {

// Complete node catalog of the engine, in the order the node menu shows it.
// The categories and names follow the reference art (Core / Visual / Logic /
// Input / Physics / Custom); the engine attaches real components wherever a
// matching one exists and reports an honest note for the rest.
enum class NodeCategory : std::uint8_t { core, visual, logic, input, physics, custom_script };

enum class NodeKind : std::uint8_t {
    node2d,
    canvas_layer,
    y_sort,
    sprite2d,
    animated_sprite2d,
    texture_rect,
    tile_map,
    tile_map_layer,
    line2d,
    polygon2d,
    label,
    rich_text_label,
    button,
    panel,
    color_rect,
    texture_button,
    timer,
    animation_player,
    tween,
    state_machine,
    signal_bus,
    audio_stream_player2d,
    input_handler,
    keybind,
    touch_screen_button,
    collision_shape2d,
    area2d,
    rigid_body2d,
    character_body2d,
    static_body2d,
    custom
};

// One catalog entry. `summary` is the tooltip text of the menu. The fields are
// string_view so the catalog itself can live in a constexpr table.
struct NodeTypeInfo {
    NodeKind kind{};
    NodeCategory category{};
    std::string_view name;
    std::string_view summary;
};

// What creating this node actually produces: the entity name plus the engine
// components that are attached. `note` is reported to the console so the user
// learns exactly which parts of the node have no engine behaviour yet.
struct NodePlacement {
    NodeKind kind{};
    std::string name;
    bool sprite{false};
    bool collider{false};
    bool motion{false};
    bool camera{false};
    bool audio{false};
    bool script{false};
    bool tile_map{false};
    std::string note;
};

// The whole catalog, in menu order (Core, Visual, Logic, Input, Physics).
[[nodiscard]] const std::vector<NodeTypeInfo>& node_catalog();

// Catalog entries of one category, keeping the catalog order.
[[nodiscard]] std::vector<NodeTypeInfo> node_catalog_of(NodeCategory category);

[[nodiscard]] const NodeTypeInfo* node_type_info(NodeKind kind);

[[nodiscard]] std::string_view node_category_name(NodeCategory category);

// The components the engine supports for a node of this kind, and an honest
// note describing what is not available yet.
[[nodiscard]] NodePlacement node_placement(NodeKind kind);

// Total number of catalog entries (static assert friendly).
[[nodiscard]] std::size_t node_catalog_size();

} // namespace gridengine
