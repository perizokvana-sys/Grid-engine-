// GridEngine - include/gridengine/editor.hpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#pragma once

#include "gridengine/audio_device.hpp"
#include "gridengine/node_types.hpp"
#include "gridengine/scene.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gridengine {

// UI-independent editor state and operations. A future frontend can render this
// hierarchy and selection in its Hierarchy and Inspector panels.
struct HierarchyItem {
    EntityId id{};
    std::size_t depth{};
};

class Editor final {
public:
    [[nodiscard]] Scene& scene() noexcept { return scene_; }
    [[nodiscard]] const Scene& scene() const noexcept { return scene_; }
    [[nodiscard]] std::optional<EntityId> selection() const noexcept { return selection_; }
    [[nodiscard]] const Entity* inspected_entity() const noexcept;
    [[nodiscard]] std::vector<HierarchyItem> hierarchy() const;

    [[nodiscard]] EntityId create_entity(std::string name = "Entity");
    [[nodiscard]] bool select(std::optional<EntityId> id) noexcept;
    [[nodiscard]] bool remove_selected();
    [[nodiscard]] std::optional<EntityId> duplicate_selected();
    [[nodiscard]] bool reparent_selected(std::optional<EntityId> parent, bool keep_world_transform = true);
    [[nodiscard]] bool rename_selected(std::string name);
    [[nodiscard]] bool set_transform(Transform2D value) noexcept;
    [[nodiscard]] bool set_sprite(std::optional<Sprite2D> value);
    [[nodiscard]] bool set_camera(std::optional<Camera2D> value) noexcept;
    [[nodiscard]] bool set_collider(std::optional<Collider2D> value) noexcept;
    [[nodiscard]] bool set_motion(std::optional<Motion2D> value) noexcept;
    [[nodiscard]] bool set_audio(std::optional<AudioSource> value) noexcept;
    [[nodiscard]] bool set_script(std::string source);
    [[nodiscard]] bool save_scene(const std::filesystem::path& file) const;
    [[nodiscard]] bool load_scene(const std::filesystem::path& file);

    // Clears the scene, selection, console and play state without recreating
    // the editor object (it owns a non-copyable audio device).
    void reset() noexcept;

    // Play mode: the scene is stepped every frame with the editor's audio
    // device. Stopping restores the state the scene had when Play was pressed,
    // so editing never destroys the authored content.
    [[nodiscard]] bool playing() const noexcept { return playing_; }
    void start_play();
    void stop_play();
    // Advances the playing scene by `delta` seconds and reports script errors
    // through the console log.
    void step(double delta_seconds);

    // Console: the frontend prints these lines; audio failures land here too.
    [[nodiscard]] const std::vector<std::string>& console_lines() const noexcept { return console_; }
    void log(std::string line);
    void clear_console() noexcept { console_.clear(); }
    [[nodiscard]] AudioDevice& audio() noexcept { return audio_; }

    // Creates a node from the catalog: the entity plus every engine component
    // the node maps to. What is not available yet is reported on the console.
    // Returns 0 when the node could not be created.
    [[nodiscard]] EntityId add_node(NodeKind kind, std::string_view script_source = {});
    // Node catalog kind of the selected entity as it was created.
    [[nodiscard]] std::optional<NodeKind> created_node(EntityId id) const noexcept;
    // A script-only node ("Custom"): attaches a Grid script to a new entity.
    [[nodiscard]] EntityId add_script_node(std::string_view name, std::string_view source);

    // --- GridScene 7: frameset animation -------------------------------------
    // Adds a frameset to the open scene and returns its index.
    [[nodiscard]] int add_frameset(AnimationFrameset set);
    // The scene's framesets, edited by the frameset editor panel.
    [[nodiscard]] std::vector<AnimationFrameset>& framesets() noexcept;
    [[nodiscard]] const std::vector<AnimationFrameset>& framesets() const noexcept;
    // Plays a frameset clip: on the selected entity, or on a given one.
    [[nodiscard]] bool set_animation(int frameset_index, int clip_index, double fps = 0.0);
    [[nodiscard]] bool set_animation(EntityId id, int frameset_index, int clip_index,
                                     double fps = 0.0);
    // Advances every sprite's animation; returns true when a frame moved, so
    // preview-only frames can be skipped when nothing animates.
    [[nodiscard]] bool advance_animations(double delta_seconds);

private:
    void append_hierarchy(EntityId id, std::size_t depth, std::vector<HierarchyItem>& out) const;
    bool valid_motion(const Motion2D& m) const noexcept;
    bool valid_collider(const Collider2D& c) const noexcept;
    void advance_timers(double delta_seconds);
    Scene scene_;
    Scene play_snapshot_;
    std::optional<EntityId> selection_;
    std::vector<std::string> console_;
    AudioDevice audio_;
    bool playing_{false};
    // Runtime state of Timer nodes: not part of the scene file, only live while
    // the editor process runs.
    struct TimerState {
        EntityId id{};
        std::string name;
        double remaining{1.0};
        double period{1.0};
        bool loops{true};
    };
    std::vector<TimerState> timers_;
    std::vector<std::pair<EntityId, NodeKind>> created_nodes_;
};

} // namespace gridengine
