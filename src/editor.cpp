// GridEngine - src/editor.cpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#include "gridengine/editor.hpp"
#include "gridengine/grid.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace gridengine {
namespace {
bool valid_transform(const Transform2D& t) noexcept {
    return std::isfinite(t.position.x) && std::isfinite(t.position.y) &&
           std::isfinite(t.rotation) && std::isfinite(t.scale.x) && std::isfinite(t.scale.y);
}
} // namespace

bool Editor::valid_motion(const Motion2D& m) const noexcept {
    return std::isfinite(m.velocity.x) && std::isfinite(m.velocity.y) &&
           std::isfinite(m.angular_velocity);
}

bool Editor::valid_collider(const Collider2D& c) const noexcept {
    return std::isfinite(c.half_extents.x) && std::isfinite(c.half_extents.y) &&
           c.half_extents.x >= 0.0 && c.half_extents.y >= 0.0;
}

const Entity* Editor::inspected_entity() const noexcept {
    return selection_ ? scene_.find(*selection_) : nullptr;
}

void Editor::append_hierarchy(EntityId id, std::size_t depth, std::vector<HierarchyItem>& out) const {
    const auto* entity = scene_.find(id);
    if (!entity) return;
    out.push_back({id, depth});
    for (const auto child : entity->children) append_hierarchy(child, depth + 1, out);
}

std::vector<HierarchyItem> Editor::hierarchy() const {
    std::vector<HierarchyItem> result;
    for (const auto id : scene_.roots()) append_hierarchy(id, 0, result);
    return result;
}

EntityId Editor::create_entity(std::string name) {
    const auto id = scene_.create_entity(std::move(name));
    if (id) selection_ = id;
    return id;
}

bool Editor::select(std::optional<EntityId> id) noexcept {
    if (id && !scene_.find(*id)) return false;
    selection_ = id;
    return true;
}

bool Editor::remove_selected() {
    if (!selection_ || !scene_.remove_entity(*selection_)) return false;
    selection_.reset();
    return true;
}

std::optional<EntityId> Editor::duplicate_selected() {
    if (!selection_) return {};
    const auto duplicate = scene_.duplicate_entity(*selection_);
    if (duplicate) selection_ = *duplicate;
    return duplicate;
}

bool Editor::reparent_selected(std::optional<EntityId> parent, bool keep_world_transform) {
    return selection_ && scene_.set_parent(*selection_, parent, keep_world_transform);
}

bool Editor::rename_selected(std::string name) {
    return selection_ && scene_.rename_entity(*selection_, std::move(name));
}

bool Editor::set_transform(Transform2D value) noexcept {
    auto* entity = selection_ ? scene_.find(*selection_) : nullptr;
    if (!entity || !valid_transform(value)) return false;
    entity->transform = value;
    return true;
}

bool Editor::set_sprite(std::optional<Sprite2D> value) {
    auto* entity = selection_ ? scene_.find(*selection_) : nullptr;
    if (!entity || (value && value->texture.find('\0') != std::string::npos)) return false;
    entity->sprite = std::move(value);
    return true;
}

bool Editor::set_camera(std::optional<Camera2D> value) noexcept {
    auto* entity = selection_ ? scene_.find(*selection_) : nullptr;
    if (!entity || (value && (!std::isfinite(value->zoom) || value->zoom <= 0.0))) return false;
    entity->camera = value;
    return true;
}

bool Editor::set_collider(std::optional<Collider2D> value) noexcept {
    auto* entity = selection_ ? scene_.find(*selection_) : nullptr;
    if (!entity || (value && !valid_collider(*value))) return false;
    entity->collider = std::move(value);
    return true;
}

bool Editor::set_motion(std::optional<Motion2D> value) noexcept {
    auto* entity = selection_ ? scene_.find(*selection_) : nullptr;
    if (!entity || (value && !valid_motion(*value))) return false;
    entity->motion = std::move(value);
    return true;
}

bool Editor::set_audio(std::optional<AudioSource> value) noexcept {
    auto* entity = selection_ ? scene_.find(*selection_) : nullptr;
    if (!entity || (value && !value->valid())) return false;
    entity->audio = std::move(value);
    return true;
}

bool Editor::set_script(std::string source) {
    auto* entity = selection_ ? scene_.find(*selection_) : nullptr;
    if (!entity || source.find('\0') != std::string::npos) return false;
    entity->script = std::move(source);
    return true;
}

void Editor::log(std::string line) {
    console_.push_back(std::move(line));
    if (console_.size() > 512) console_.erase(console_.begin());
}

void Editor::start_play() {
    if (playing_) return;
    play_snapshot_ = scene_;
    playing_ = true;
    std::string error;
    if (!audio_.initialize(error)) log("audio: " + error);
    // Audio sources start once when Play begins; looping voices keep playing.
    for (const auto id : scene_.entities()) {
        const auto* entity = scene_.find(id);
        if (!entity || !entity->audio || entity->audio->resource.empty()) continue;
        std::string play_error;
        const auto world = scene_.world_transform(id);
        if (!audio_.play(*entity->audio, {}, world.position, true, play_error))
            log(entity->name + ": " + play_error);
    }
    log("play: started");
}

void Editor::stop_play() {
    if (!playing_) return;
    scene_ = play_snapshot_;
    playing_ = false;
    audio_.stop_all();
    log("play: stopped");
}

void Editor::step(double delta_seconds) {
    if (!playing_) return;
    audio_.update();
    // Grid scripts run first, then the physics-free motion integration.
    for (const auto id : scene_.entities()) {
        const auto* entity = scene_.find(id);
        if (!entity || entity->script.empty()) continue;
        const auto result = grid::run_on_entity(entity->script, scene_, id, delta_seconds);
        if (!result.success) log(entity->name + ": " + result.diagnostic.message);
    }
    advance_timers(delta_seconds);
    audio_.update();
    if (!scene_.update(delta_seconds)) log("scene: update rejected");
    // Sprite sheet animation advances with the frame rate, after scripts and
    // motion, so a moving character also animates while played.
    (void)scene_.advance_animations(delta_seconds);
    // Audio failures collected while playing surface on the console.
    for (const auto& reason : audio_.failures())
        if (std::find(console_.begin(), console_.end(), "audio: " + reason) == console_.end())
            log("audio: " + reason);
}

int Editor::add_frameset(AnimationFrameset set) { return scene_.add_frameset(std::move(set)); }

std::vector<AnimationFrameset>& Editor::framesets() noexcept { return scene_.animation_framesets(); }

const std::vector<AnimationFrameset>& Editor::framesets() const noexcept {
    return scene_.animation_framesets();
}

bool Editor::set_animation(int frameset_index, int clip_index, double fps) {
    if (!selection_) return false;
    return scene_.set_animation(*selection_, frameset_index, clip_index, fps);
}

bool Editor::set_animation(EntityId id, int frameset_index, int clip_index, double fps) {
    return scene_.set_animation(id, frameset_index, clip_index, fps);
}

bool Editor::advance_animations(double delta_seconds) {
    return scene_.advance_animations(delta_seconds);
}

void Editor::reset() noexcept {
    stop_play();
    scene_ = Scene{};
    play_snapshot_ = Scene{};
    selection_.reset();
    console_.clear();
    playing_ = false;
    timers_.clear();
    created_nodes_.clear();
}

bool Editor::save_scene(const std::filesystem::path& file) const {
    return scene_.save(file);
}

bool Editor::load_scene(const std::filesystem::path& file) {
    auto loaded = Scene::load(file);
    if (!loaded) return false;
    scene_ = std::move(*loaded);
    selection_.reset();
    console_.clear();
    console_.push_back("scene: loaded " + file.filename().string());
    return true;
}

EntityId Editor::add_node(NodeKind kind, std::string_view script_source) {
    const NodePlacement placement = node_placement(kind);
    if (placement.name.empty()) return 0;
    const EntityId id = scene_.create_entity(placement.name);
    if (!id) return 0;
    selection_ = id;
    created_nodes_.emplace_back(id, kind);

    auto* entity = scene_.find(id);
    if (!entity) return 0;
    if (placement.sprite) {
        Sprite2D sprite;
        sprite.size = {32.0, 32.0};
        // ColorRect starts as a visible neutral rectangle; the others wait for a
        // texture so the author picks an asset.
        if (kind == NodeKind::color_rect) sprite.color = 0x4c5b66ffu;
        entity->sprite = sprite;
    }
    if (placement.collider) {
        Collider2D collider;
        collider.half_extents = {16.0, 16.0};
        collider.enabled = true;
        entity->collider = collider;
    }
    if (placement.motion) {
        Motion2D motion;
        motion.velocity = {60.0, 0.0};
        entity->motion = motion;
    }
    if (placement.camera) {
        Camera2D camera;
        camera.enabled = true;
        camera.zoom = 1.0;
        entity->camera = camera;
    }
    if (placement.audio) {
        AudioSource source;
        source.resource = "assets/";
        source.volume = 1.0f;
        entity->audio = source;
    }
    if (placement.tile_map) {
        // The tile map lives in scenes/main.tiles, not inside an entity, so the
        // node only marks the place and the console explains how to paint.
    }
    if (placement.script || !script_source.empty()) {
        // A custom node carries the Grid script it was created with.
        if (!script_source.empty()) entity->script = std::string(script_source);
    }
    if (kind == NodeKind::timer) {
        TimerState timer;
        timer.id = id;
        timer.name = placement.name;
        timer.period = 1.0;
        timer.remaining = 1.0;
        timer.loops = true;
        timers_.push_back(timer);
    }
    if (!placement.note.empty()) log(placement.name + ": " + placement.note);
    log("added node: " + placement.name);
    return id;
}

EntityId Editor::add_script_node(std::string_view name, std::string_view source) {
    if (name.empty()) return 0;
    const EntityId id = add_node(NodeKind::custom, source);
    if (!id) return 0;
    if (auto* entity = scene_.find(id)) entity->name = std::string(name);
    return id;
}

std::optional<NodeKind> Editor::created_node(EntityId id) const noexcept {
    for (const auto& entry : created_nodes_)
        if (entry.first == id) return entry.second;
    return std::nullopt;
}

void Editor::advance_timers(double delta_seconds) {
    if (!std::isfinite(delta_seconds) || delta_seconds < 0.0) return;
    for (auto& timer : timers_) {
        if (const auto* entity = scene_.find(timer.id)) timer.name = entity->name;
        timer.remaining -= delta_seconds;
        while (timer.remaining <= 0.0) {
            log(timer.name + ": timer fired");
            if (!timer.loops) { timer.period = 0.0; break; }
            timer.remaining += timer.period;
        }
    }
    std::erase_if(timers_, [](const TimerState& timer) { return timer.period <= 0.0; });
}

} // namespace gridengine
