// GridEngine - src/scene.cpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#include "gridengine/scene.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <unordered_set>

namespace gridengine {
namespace {
constexpr double pi = 3.14159265358979323846;
constexpr double epsilon = 1e-12;
bool finite(const Transform2D& t) {
    return std::isfinite(t.position.x) && std::isfinite(t.position.y) && std::isfinite(t.rotation) &&
           std::isfinite(t.scale.x) && std::isfinite(t.scale.y);
}
bool finite(const Motion2D& m) {
    return std::isfinite(m.velocity.x) && std::isfinite(m.velocity.y) && std::isfinite(m.angular_velocity);
}
bool valid(const Collider2D& c) {
    return std::isfinite(c.half_extents.x) && std::isfinite(c.half_extents.y) &&
           c.half_extents.x >= 0.0 && c.half_extents.y >= 0.0;
}
Vec2 rotate(Vec2 p, double degrees) {
    const double a = degrees * pi / 180.0;
    return {p.x * std::cos(a) - p.y * std::sin(a), p.x * std::sin(a) + p.y * std::cos(a)};
}
Transform2D combine(const Transform2D& parent, const Transform2D& local) {
    Vec2 offset{local.position.x * parent.scale.x, local.position.y * parent.scale.y};
    offset = rotate(offset, parent.rotation);
    return {{parent.position.x + offset.x, parent.position.y + offset.y}, parent.rotation + local.rotation,
            {parent.scale.x * local.scale.x, parent.scale.y * local.scale.y}};
}
Transform2D relative(const Transform2D& parent, const Transform2D& world) {
    Vec2 d{world.position.x - parent.position.x, world.position.y - parent.position.y};
    d = rotate(d, -parent.rotation);
    return {{d.x / parent.scale.x, d.y / parent.scale.y}, world.rotation - parent.rotation,
            {world.scale.x / parent.scale.x, world.scale.y / parent.scale.y}};
}
}

Entity* Scene::find(EntityId id) noexcept {
    auto i = std::find_if(entities_.begin(), entities_.end(), [=](const Entity& e) { return e.id == id; });
    return i == entities_.end() ? nullptr : &*i;
}
const Entity* Scene::find(EntityId id) const noexcept {
    auto i = std::find_if(entities_.begin(), entities_.end(), [=](const Entity& e) { return e.id == id; });
    return i == entities_.end() ? nullptr : &*i;
}
EntityId Scene::create_entity(std::string name, std::optional<EntityId> parent) {
    if ((parent && !find(*parent)) || next_id_ == 0 || next_id_ == std::numeric_limits<EntityId>::max()) return 0;
    const EntityId id = next_id_++;
    entities_.push_back(Entity{id, std::move(name)});
    if (parent) { entities_.back().parent = parent; find(*parent)->children.push_back(id); }
    return id;
}
bool Scene::rename_entity(EntityId id, std::string name) {
    auto* e = find(id); if (!e || name.empty()) return false; e->name = std::move(name); return true;
}
Transform2D Scene::world_transform(EntityId id) const {
    const auto* e = find(id); if (!e) return {};
    if (!e->parent) return e->transform;
    const auto* p = find(*e->parent); return p ? combine(world_transform(p->id), e->transform) : e->transform;
}
bool Scene::set_parent(EntityId child, std::optional<EntityId> parent, bool keep_world_transform) {
    auto* c = find(child); if (!c || (parent && (!find(*parent) || *parent == child))) return false;
    for (auto cursor = parent; cursor;) {
        if (*cursor == child) return false;
        const auto* e = find(*cursor); cursor = e ? e->parent : std::nullopt;
    }
    const Transform2D old_world = world_transform(child);
    Transform2D new_local = old_world;
    if (parent && keep_world_transform) {
        const auto target_world = world_transform(*parent);
        if (std::abs(target_world.scale.x) < epsilon || std::abs(target_world.scale.y) < epsilon ||
            !finite(target_world) || !finite(old_world)) return false;
        new_local = relative(target_world, old_world);
        if (!finite(new_local)) return false;
    }
    if (c->parent) if (auto* old = find(*c->parent)) std::erase(old->children, child);
    c->parent = parent;
    if (parent) find(*parent)->children.push_back(child);
    if (keep_world_transform) c->transform = new_local;
    return true;
}
bool Scene::update(double delta_seconds) {
    if (!std::isfinite(delta_seconds) || delta_seconds < 0.0) return false;

    // Stage every local transform in a copy. Any invalid transform, motion, or
    // parent scale aborts without exposing a partially advanced scene.
    Scene staged = *this;
    for (const auto& e : staged.entities_) {
        if (!finite(e.transform) || (e.motion && !finite(*e.motion))) return false;
    }
    for (const auto id : staged.roots()) {
        std::vector<EntityId> stack{id};
        while (!stack.empty()) {
            const auto current = stack.back(); stack.pop_back();
            auto* e = staged.find(current);
            const auto before = staged.world_transform(current);
            if (!finite(before)) return false;
            Transform2D desired = before;
            if (e->motion) {
                desired.position.x += e->motion->velocity.x * delta_seconds;
                desired.position.y += e->motion->velocity.y * delta_seconds;
                desired.rotation += e->motion->angular_velocity * delta_seconds;
            }
            if (!finite(desired)) return false;
            if (e->parent) {
                const auto parent_world = staged.world_transform(*e->parent);
                if (!finite(parent_world) || std::abs(parent_world.scale.x) < epsilon ||
                    std::abs(parent_world.scale.y) < epsilon) return false;
                e->transform = relative(parent_world, desired);
            } else e->transform = desired;
            if (!finite(e->transform)) return false;
            for (auto it = e->children.rbegin(); it != e->children.rend(); ++it) stack.push_back(*it);
        }
    }
    for (const auto& e : staged.entities_) {
        auto* original = find(e.id);
        if (!original) return false;
        original->transform = e.transform;
    }
    return true;
}
std::vector<std::pair<EntityId, EntityId>> Scene::collisions() const {
    struct Box { EntityId id; double min_x, min_y, max_x, max_y; };
    std::vector<Box> boxes;
    for (const auto& e : entities_) {
        if (!e.collider || !e.collider->enabled || !valid(*e.collider)) continue;
        const auto t = world_transform(e.id);
        if (!finite(t)) continue;
        // A rotated local rectangle is conservatively enclosed by its world AABB.
        const double hx = e.collider->half_extents.x * std::abs(t.scale.x);
        const double hy = e.collider->half_extents.y * std::abs(t.scale.y);
        const double radians = t.rotation * pi / 180.0;
        const double ex = std::abs(std::cos(radians)) * hx + std::abs(std::sin(radians)) * hy;
        const double ey = std::abs(std::sin(radians)) * hx + std::abs(std::cos(radians)) * hy;
        boxes.push_back({e.id, t.position.x - ex, t.position.y - ey, t.position.x + ex, t.position.y + ey});
    }
    std::vector<std::pair<EntityId, EntityId>> result;
    for (std::size_t i = 0; i < boxes.size(); ++i) for (std::size_t j = i + 1; j < boxes.size(); ++j) {
        const auto& a = boxes[i]; const auto& b = boxes[j];
        if (a.min_x <= b.max_x && a.max_x >= b.min_x && a.min_y <= b.max_y && a.max_y >= b.min_y)
            result.emplace_back(std::min(a.id, b.id), std::max(a.id, b.id));
    }
    std::sort(result.begin(), result.end());
    return result;
}
bool Scene::remove_entity(EntityId id) {
    auto* e = find(id); if (!e) return false;
    const auto children = e->children;
    for (auto child : children) remove_entity(child);
    e = find(id); if (e && e->parent) if (auto* p = find(*e->parent)) std::erase(p->children, id);
    std::erase_if(entities_, [=](const Entity& item) { return item.id == id; }); return true;
}
std::optional<EntityId> Scene::duplicate_entity(EntityId id) {
    const auto* original = find(id); if (!original) return {};
    const Entity copy = *original;
    const auto clone = create_entity(copy.name + " Copy", copy.parent);
    if (!clone) return {};
    auto* target = find(clone); target->transform = copy.transform; target->motion = copy.motion;
    target->collider = copy.collider; target->sprite = copy.sprite; target->camera = copy.camera;
    target->audio = copy.audio; target->script = copy.script;
    for (auto child : copy.children) {
        auto cloned_child = duplicate_entity(child);
        if (!cloned_child || !set_parent(*cloned_child, clone, false)) {
            remove_entity(clone); if (cloned_child) remove_entity(*cloned_child); return {};
        }
    }
    return clone;
}
std::vector<EntityId> Scene::roots() const { std::vector<EntityId> r; for (const auto& e : entities_) if (!e.parent) r.push_back(e.id); return r; }
std::vector<EntityId> Scene::entities() const { std::vector<EntityId> r; for (const auto& e : entities_) r.push_back(e.id); return r; }

bool Scene::save(const std::filesystem::path& file) const {
    for (const auto& e : entities_) if (!e.id || !finite(e.transform) || e.name.empty() ||
        (e.motion && !finite(*e.motion)) || (e.collider && !valid(*e.collider)) ||
        (e.sprite && e.sprite->texture.find('\0') != std::string::npos) ||
        (e.sprite && !std::isfinite(e.sprite->elapsed)) ||
        (e.camera && (!std::isfinite(e.camera->zoom) || e.camera->zoom <= 0.0)) ||
        (e.audio && !e.audio->valid())) return false;
    for (const auto& set : framesets_)
        if (set.texture.find('\0') != std::string::npos) return false;
    // Scripts are a format 4 addition; sprite sizes are a format 5 addition;
    // audio sources are a format 6 addition; frameset animation is a format 7
    // addition. The writer only bumps the version when the data actually needs
    // it, so legacy scenes stay at versions 1-3.
    const bool has_script = std::any_of(entities_.begin(), entities_.end(),
                                          [](const Entity& e) { return !e.script.empty(); });
    const bool has_sprite_size = std::any_of(entities_.begin(), entities_.end(), [](const Entity& e) {
        return e.sprite && (e.sprite->size.x != 32.0 || e.sprite->size.y != 32.0);
    });
    const bool has_audio = std::any_of(entities_.begin(), entities_.end(),
                                         [](const Entity& e) { return e.audio.has_value(); });
    const bool has_animation = !framesets_.empty() ||
                               std::any_of(entities_.begin(), entities_.end(),
                                           [](const Entity& e) { return e.sprite && e.sprite->animating(); });
    // Scripts are a format 4 addition; sprite sizes are a format 5 addition;
    // audio sources are a format 6 addition; frameset animation is a format 7
    // addition. The writer only bumps the version when the data actually needs
    // it, so legacy scenes stay at versions 1-3. From version 5 on the sprite
    // size is always written, because the reader always reads it back.
    int format = has_script ? 4 : 3;
    if (has_sprite_size) format = 5;
    if (has_audio) format = 6;
    if (has_animation) format = 7;
    std::ofstream out(file, std::ios::binary); if (!out) return false;
    out << "GridScene " << format << "\n" << std::setprecision(17);
    // Format 7 stores the animation framesets first; entities reference them
    // by index so a shared sheet is written exactly once.
    if (format >= 7) {
        for (const auto& set : framesets_) {
            out << "FS " << std::quoted(set.texture) << ' ' << set.columns << ' ' << set.rows << ' '
                << set.margin << ' ' << set.spacing << ' ' << set.frames.size() << ' ';
            for (const auto& f : set.frames) out << f.x << ' ' << f.y << ' ' << f.w << ' ' << f.h << ' ';
            out << set.clips.size() << ' ';
            for (const auto& clip : set.clips) {
                out << std::quoted(clip.name) << ' ' << clip.fps << ' ' << clip.loop << ' '
                    << clip.delay << ' ' << clip.frames.size() << ' ';
                for (const int frame : clip.frames) out << frame << ' ';
            }
            out << '\n';
        }
    }
    for (const auto& e : entities_) {
        out << "E " << e.id << ' ' << (e.parent ? *e.parent : 0) << ' ' << std::quoted(e.name) << ' '
            << e.transform.position.x << ' ' << e.transform.position.y << ' ' << e.transform.rotation << ' '
            << e.transform.scale.x << ' ' << e.transform.scale.y << ' ' << bool(e.motion) << ' ';
        if (e.motion) out << e.motion->velocity.x << ' ' << e.motion->velocity.y << ' ' << e.motion->angular_velocity << ' ';
        out << bool(e.collider) << ' ';
        if (e.collider) out << e.collider->half_extents.x << ' ' << e.collider->half_extents.y << ' ' << e.collider->enabled << ' ';
        out << bool(e.sprite) << ' ';
        if (e.sprite) {
            out << std::quoted(e.sprite->texture) << ' ';
            // From format 5 on the size is always present, matching the reader.
            if (format >= 5) out << e.sprite->size.x << ' ' << e.sprite->size.y << ' ';
            out << e.sprite->visible << ' ' << e.sprite->color << ' ';
            // Format 7 stores the sprite's animation state after the color.
            if (format >= 7) {
                out << (e.sprite->animating() ? e.sprite->frameset : -1) << ' ' << e.sprite->clip << ' '
                    << e.sprite->frame << ' ' << e.sprite->elapsed << ' ' << e.sprite->playing << ' '
                    << e.sprite->frame_x << ' ' << e.sprite->frame_y << ' ' << e.sprite->frame_w << ' '
                    << e.sprite->frame_h << ' ';
            }
        }
        out << bool(e.camera) << ' ';
        if (e.camera) out << e.camera->enabled << ' ' << e.camera->zoom << ' ';
        if (format >= 4) out << std::quoted(e.script) << ' ';
        // Format 6 stores the audio source: resource, volume, mute, loop.
        if (format >= 6) {
            out << bool(e.audio) << ' ';
            if (e.audio) out << std::quoted(e.audio->resource.string()) << ' ' << e.audio->volume << ' '
                             << e.audio->muted << ' ' << e.audio->loop << ' ';
        }
        out << '\n';
    }
    return bool(out);
}
std::optional<Scene> Scene::load(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary); std::string signature; int format{};
    if (!(in >> signature >> format) || signature != "GridScene" || (format < 1 || format > 7)) return {};
    Scene scene; std::string tag; std::unordered_set<EntityId> ids;
    while (in >> tag) {
        // Format 7 stores animation framesets; entities reference them by index.
        if (tag == "FS") {
            if (format < 7) return {};
            AnimationFrameset set;
            std::size_t frame_count{};
            if (!(in >> std::quoted(set.texture) >> set.columns >> set.rows >> set.margin >>
                  set.spacing >> frame_count) ||
                set.columns <= 0 || set.rows <= 0 || set.margin < 0 || set.spacing < 0)
                return {};
            set.frames.resize(frame_count);
            for (auto& frame : set.frames)
                if (!(in >> frame.x >> frame.y >> frame.w >> frame.h) || frame.w <= 0 || frame.h <= 0)
                    return {};
            std::size_t clip_count{};
            if (!(in >> clip_count)) return {};
            set.clips.resize(clip_count);
            for (auto& clip : set.clips) {
                std::size_t count{};
                if (!(in >> std::quoted(clip.name) >> clip.fps >> clip.loop >> clip.delay >> count) ||
                    clip.name.empty() || !std::isfinite(clip.fps) || clip.fps < 0.0 ||
                    !std::isfinite(clip.delay) || clip.delay < 0.0 || clip.delay > 60.0)
                    return {};
                clip.frames.resize(count);
                for (auto& frame : clip.frames)
                    if (!(in >> frame) || frame < 0 || frame >= static_cast<int>(set.frames.size()))
                        return {};
            }
            scene.framesets_.push_back(std::move(set));
            continue;
        }
        if (tag != "E") return {};
        Entity e; EntityId parent{}; bool has_sprite{}, has_camera{}, has_motion{}, has_collider{};
        if (!(in >> e.id >> parent >> std::quoted(e.name) >> e.transform.position.x >> e.transform.position.y
              >> e.transform.rotation >> e.transform.scale.x >> e.transform.scale.y) ||
            (format >= 2 && !(in >> has_motion)) || !e.id || e.name.empty() || !ids.insert(e.id).second || !finite(e.transform)) return {};
        if (has_motion) { Motion2D m; if (!(in >> m.velocity.x >> m.velocity.y >> m.angular_velocity) || !finite(m)) return {}; e.motion = m; }
        if (format >= 3 && !(in >> has_collider)) return {};
        if (has_collider) { Collider2D c; if (!(in >> c.half_extents.x >> c.half_extents.y >> c.enabled) || !valid(c)) return {}; e.collider = c; }
        if (!(in >> has_sprite)) return {};
        if (has_sprite) {
            Sprite2D s;
            if (!(in >> std::quoted(s.texture))) return {};
            // Format 5 stores the sprite size; older formats use the default.
            if (format >= 5 && !(in >> s.size.x >> s.size.y)) return {};
            if (!(in >> s.visible >> s.color) || !std::isfinite(s.size.x) || !std::isfinite(s.size.y) ||
                s.size.x <= 0.0 || s.size.y <= 0.0) return {};
            if (s.color == 0) return {};
            // Format 7 stores the sprite's animation state after the color.
            if (format >= 7) {
                int index{};
                if (!(in >> index >> s.clip >> s.frame >> s.elapsed >> s.playing >> s.frame_x >>
                      s.frame_y >> s.frame_w >> s.frame_h) ||
                    !std::isfinite(s.elapsed) || s.elapsed < 0.0)
                    return {};
                s.frameset = index;
                if (s.frameset >= 0) {
                    if (s.frameset >= static_cast<int>(scene.framesets_.size())) return {};
                    const auto& set = scene.framesets_[s.frameset];
                    if (s.clip < 0 || s.clip >= static_cast<int>(set.clips.size())) return {};
                    if (s.frame < -1) return {};
                }
            }
            e.sprite = std::move(s);
        }
        if (!(in >> has_camera)) return {};
        if (has_camera) { Camera2D c; if (!(in >> c.enabled >> c.zoom) || !std::isfinite(c.zoom) || c.zoom <= 0.0) return {}; e.camera = c; }
        // Format 4 adds a quoted Grid script per entity. Formats 1-3 have no
        // entity script, so the version alone decides whether one is read.
        if (format >= 4 && !(in >> std::quoted(e.script))) return {};
        // Format 6 adds an audio source. Formats 1-5 carry no audio.
        if (format >= 6) {
            bool has_audio{};
            if (!(in >> has_audio)) return {};
            if (has_audio) {
                AudioSource a;
                std::string resource;
                if (!(in >> std::quoted(resource) >> a.volume >> a.muted >> a.loop)) return {};
                a.resource = std::filesystem::path(resource);
                e.audio = std::move(a);
            }
        }
        e.parent = parent ? std::optional<EntityId>(parent) : std::nullopt;
        if (e.id == std::numeric_limits<EntityId>::max()) return {};
        scene.next_id_ = std::max(scene.next_id_, e.id + 1); scene.entities_.push_back(std::move(e));
    }
    if (!in.eof()) return {};
    for (auto& e : scene.entities_) if (e.parent) {
        auto* p = scene.find(*e.parent); if (!p || *e.parent == e.id) return {}; p->children.push_back(e.id);
    }
    for (const auto& e : scene.entities_) {
        std::unordered_set<EntityId> seen; auto cur = e.parent;
        while (cur) { if (!seen.insert(*cur).second) return {}; const auto* p = scene.find(*cur); if (!p) return {}; cur = p->parent; }
    }
    return scene;
}

// --- GridScene 7: frameset slicing ------------------------------------------
void AnimationFrameset::slice_grid(int texture_width, int texture_height) {
    frames.clear();
    const int col_count = std::max(1, columns);
    const int row_count = std::max(1, rows);
    const int sheet_margin = std::max(0, margin);
    const int cell_spacing = std::max(0, spacing);
    if (texture_width <= 2 * sheet_margin || texture_height <= 2 * sheet_margin) return;
    const int usable_w = texture_width - 2 * sheet_margin;
    const int usable_h = texture_height - 2 * sheet_margin;
    const int cell_w = (usable_w - (col_count - 1) * cell_spacing) / col_count;
    const int cell_h = (usable_h - (row_count - 1) * cell_spacing) / row_count;
    if (cell_w <= 0 || cell_h <= 0) return;
    frames.reserve(static_cast<std::size_t>(col_count) * row_count);
    for (int row = 0; row < row_count; ++row) {
        for (int col = 0; col < col_count; ++col) {
            AnimationFrame frame;
            frame.x = sheet_margin + col * (cell_w + cell_spacing);
            frame.y = sheet_margin + row * (cell_h + cell_spacing);
            frame.w = cell_w;
            frame.h = cell_h;
            frames.push_back(frame);
        }
    }
}

const AnimationClip* AnimationFrameset::clip(int index) const noexcept {
    if (index < 0 || index >= static_cast<int>(clips.size())) return nullptr;
    return &clips[static_cast<std::size_t>(index)];
}

int Scene::add_frameset(AnimationFrameset set) {
    framesets_.push_back(std::move(set));
    return static_cast<int>(framesets_.size()) - 1;
}

AnimationFrameset* Scene::frameset(int index) noexcept {
    if (index < 0 || index >= static_cast<int>(framesets_.size())) return nullptr;
    return &framesets_[static_cast<std::size_t>(index)];
}

const AnimationFrameset* Scene::frameset(int index) const noexcept {
    if (index < 0 || index >= static_cast<int>(framesets_.size())) return nullptr;
    return &framesets_[static_cast<std::size_t>(index)];
}

bool Scene::set_animation(EntityId id, int frameset_index, int clip_index, double fps) {
    auto* entity = find(id);
    if (!entity || !entity->sprite) return false;
    auto* set = frameset(frameset_index);
    if (!set) return false;
    if (set->clips.empty()) return false;
    if (clip_index < 0 || clip_index >= static_cast<int>(set->clips.size())) return false;
    auto& sprite = *entity->sprite;
    if (!sprite.texture.empty() && set->texture != sprite.texture) return false;
    if (sprite.texture.empty() && !set->texture.empty()) {
        // The frameset knows the sheet: adopting it fills in the sprite texture.
        sprite.texture = set->texture;
    }
    if (fps > 0.0) set->clips[static_cast<std::size_t>(clip_index)].fps = fps;
    sprite.frameset = frameset_index;
    sprite.clip = clip_index;
    sprite.frame = 0;
    sprite.elapsed = 0.0;
    sprite.playing = true;
    const auto& clip = set->clips[static_cast<std::size_t>(clip_index)];
    if (clip.frames.empty()) return false;
    const auto& frame = set->frames[static_cast<std::size_t>(clip.frames.front())];
    sprite.frame_x = frame.x;
    sprite.frame_y = frame.y;
    sprite.frame_w = frame.w;
    sprite.frame_h = frame.h;
    return true;
}

bool Scene::advance_animation(EntityId id, double delta_seconds) {
    auto* entity = find(id);
    if (!entity || !entity->sprite || !entity->sprite->playing) return false;
    auto& sprite = *entity->sprite;
    auto* set = frameset(sprite.frameset);
    if (!set || set->clips.empty()) return false;
    const int clip_index = std::clamp(sprite.clip, 0, static_cast<int>(set->clips.size()) - 1);
    const auto& clip = set->clips[static_cast<std::size_t>(clip_index)];
    if (clip.frames.empty()) return false;
    if (sprite.frame < 0 || sprite.frame >= static_cast<int>(clip.frames.size())) sprite.frame = 0;
    const double step = (clip.fps > 0.0 ? 1.0 / clip.fps : 0.125) + std::max(0.0, clip.delay);
    double advanced = delta_seconds;
    if (!std::isfinite(advanced) || advanced < 0.0) return false;
    sprite.elapsed += advanced;
    bool moved = false;
    while (sprite.elapsed >= step) {
        sprite.elapsed -= step;
        moved = true;
        ++sprite.frame;
        if (sprite.frame >= static_cast<int>(clip.frames.size())) {
            if (clip.loop) {
                sprite.frame = 0;
            } else {
                sprite.frame = static_cast<int>(clip.frames.size()) - 1;
                sprite.playing = false;
                break;
            }
        }
    }
    if (moved) {
        const auto& frame = set->frames[static_cast<std::size_t>(clip.frames[sprite.frame])];
        sprite.frame_x = frame.x;
        sprite.frame_y = frame.y;
        sprite.frame_w = frame.w;
        sprite.frame_h = frame.h;
    }
    return moved;
}

bool Scene::advance_animations(double delta_seconds) {
    bool any = false;
    for (const auto id : entities())
        if (advance_animation(id, delta_seconds)) any = true;
    return any;
}
} // namespace gridengine

