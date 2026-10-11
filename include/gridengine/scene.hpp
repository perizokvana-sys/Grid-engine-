// GridEngine - include/gridengine/scene.hpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#pragma once

#include "gridengine/audio.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace gridengine {
using EntityId = std::uint64_t;
struct Vec2 { double x{0.0}; double y{0.0}; };
struct Transform2D {
    Vec2 position{};
    double rotation{0.0}; // degrees
    Vec2 scale{1.0, 1.0};
};
// Linear velocity uses world units/second; angular velocity uses degrees/second.
struct Motion2D {
    Vec2 velocity{};
    double angular_velocity{0.0};
};
// Axis-aligned box in local units. Collision detection is an overlap query;
// this lightweight milestone does not apply impulses or resolve contacts.
struct Collider2D {
    Vec2 half_extents{0.5, 0.5};
    bool enabled{true};
};
struct Sprite2D {
    std::string texture;                 // project-relative path, e.g. "assets/player.png"
    Vec2 size{32.0, 32.0};               // draw size in world units (pixels at scale 1)
    bool visible{true};
    std::uint32_t color{0xffffffffu};    // RGBA packed as 0xRRGGBBAA tint
    // --- GridScene 7: sprite sheet animation (frameset slicing) -------------
    // The frameset itself lives in the scene (several sprites can share one
    // sheet). These fields are the per-sprite playback state plus the rect of
    // the current frame, resolved whenever the animation advances.
    int frameset{-1};                    // index into Scene::framesets, -1 = plain sprite
    int clip{0};                         // index into the frameset's clips
    int frame{-1};                       // index into the clip's frame list, -1 = not started
    double elapsed{0.0};                 // seconds accumulated on the current frame
    bool playing{true};                  // false freezes the animation on its frame
    // Rectangle of the current frame in sheet pixels (derived state).
    int frame_x{0};
    int frame_y{0};
    int frame_w{0};
    int frame_h{0};
    [[nodiscard]] bool animating() const noexcept { return frameset >= 0; }
    [[nodiscard]] bool has_frame() const noexcept { return frame_w > 0 && frame_h > 0; }
};
// --- GridScene 7: frameset slicing ------------------------------------------
// One rectangle cut out of a frameset sheet, in sheet pixels.
struct AnimationFrame {
    int x{0};
    int y{0};
    int w{0};
    int h{0};
};
// A named run of frames played at a fixed rate.
struct AnimationClip {
    std::string name;
    double fps{8.0};
    bool loop{true};
    double delay{0.0}; // extra hold after each frame, in seconds
    std::vector<int> frames; // indices into AnimationFrameset::frames
};
// A sheet sliced into a grid of frames plus named animation clips. Stored once
// per scene and shared by every sprite that points at it.
struct AnimationFrameset {
    std::string texture; // "assets/hero.png"
    int columns{1};
    int rows{1};
    int margin{0};   // sheet inset in pixels
    int spacing{0};  // gap between cells in pixels
    std::vector<AnimationFrame> frames;
    std::vector<AnimationClip> clips;

    // Rebuilds the frame list from the grid parameters. The cell size is
    // derived from the texture size, so a sliced sheet always covers it.
    void slice_grid(int texture_width, int texture_height);
    // Returns clip frames, or an empty list when the clip index is invalid.
    [[nodiscard]] const AnimationClip* clip(int index) const noexcept;
};

struct Camera2D {
    bool enabled{true};
    double zoom{1.0};
};
struct Entity {
    EntityId id{0};
    std::string name;
    Transform2D transform{};
    std::optional<EntityId> parent;
    std::vector<EntityId> children;
    std::optional<Motion2D> motion;
    std::optional<Collider2D> collider;
    std::optional<Sprite2D> sprite;
    std::optional<Camera2D> camera;
    // Optional audio source attached to this entity (GridScene 6+). The file
    // is resolved against the package root when the game playback runs.
    std::optional<AudioSource> audio;
    // Optional Grid script executed for this entity every frame (GridScene 4+).
    std::string script;
};

class Scene final {
public:
    [[nodiscard]] EntityId create_entity(std::string name = "Entity", std::optional<EntityId> parent = {});
    [[nodiscard]] bool remove_entity(EntityId id); // removes subtree
    [[nodiscard]] bool rename_entity(EntityId id, std::string name);
    [[nodiscard]] std::optional<EntityId> duplicate_entity(EntityId id);
    [[nodiscard]] bool set_parent(EntityId child, std::optional<EntityId> parent, bool keep_world_transform = true);
    // Advances entities with Motion2D in world space. Invalid delta or data fails atomically.
    [[nodiscard]] bool update(double delta_seconds);
    // Returns each enabled, intersecting collider pair once, ordered by EntityId.
    [[nodiscard]] std::vector<std::pair<EntityId, EntityId>> collisions() const;
    [[nodiscard]] Entity* find(EntityId id) noexcept;
    [[nodiscard]] const Entity* find(EntityId id) const noexcept;
    [[nodiscard]] std::vector<EntityId> roots() const;
    [[nodiscard]] std::vector<EntityId> entities() const;
    [[nodiscard]] Transform2D world_transform(EntityId id) const;
    [[nodiscard]] bool save(const std::filesystem::path& file) const;
    [[nodiscard]] static std::optional<Scene> load(const std::filesystem::path& file);

    // --- GridScene 7: frameset animation -------------------------------------
    // Adds a frameset and returns its index; sheet textures are shared.
    [[nodiscard]] int add_frameset(AnimationFrameset set);
    [[nodiscard]] AnimationFrameset* frameset(int index) noexcept;
    [[nodiscard]] const AnimationFrameset* frameset(int index) const noexcept;
    [[nodiscard]] std::vector<AnimationFrameset>& animation_framesets() noexcept { return framesets_; }
    [[nodiscard]] const std::vector<AnimationFrameset>& animation_framesets() const noexcept { return framesets_; }
    // Plays the current clip of a sprite: resolves the frame rect, restarts the
    // clip from its first frame and marks the sprite as animating.
    [[nodiscard]] bool set_animation(EntityId id, int frameset_index, int clip_index, double fps = 0.0);
    // Advances the playback of every sprite that is animating. Returns true
    // when any frame advanced, so callers can skip redraws if nothing moved.
    [[nodiscard]] bool advance_animations(double delta_seconds);
    // Advances one sprite. Exposed for the editor's frameset preview.
    [[nodiscard]] bool advance_animation(EntityId id, double delta_seconds);
private:
    EntityId next_id_{1};
    std::vector<Entity> entities_;
    std::vector<AnimationFrameset> framesets_;
};
} // namespace gridengine
