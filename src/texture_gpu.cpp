// GridEngine - src/texture_gpu.cpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#include "gridengine/texture_gpu.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h> // GL/gl.h needs the WGL types from the Windows headers first
#include <GL/gl.h>
#else
#include <SDL3/SDL_opengl.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <system_error>

namespace gridengine {
namespace {

constexpr double pi = 3.14159265358979323846;
Vec2 rotate(Vec2 p, double degrees) noexcept {
    const double a = degrees * pi / 180.0;
    return {p.x * std::cos(a) - p.y * std::sin(a), p.x * std::sin(a) + p.y * std::cos(a)};
}

std::string resolve_key(const std::filesystem::path& root, const std::string& texture_path) {
    std::error_code ec;
    auto resolved = std::filesystem::absolute(root / texture_path, ec).lexically_normal();
    return resolved.string();
}

} // namespace

GLTextureCache::~GLTextureCache() { clear(); }

void GLTextureCache::clear() {
    for (auto& entry : entries_) {
        if (entry.id) glDeleteTextures(1, &entry.id);
    }
    entries_.clear();
    failed_paths_.clear();
}

GLTextureCache::Entry* GLTextureCache::find(const std::string& key) {
    for (auto& entry : entries_)
        if (entry.key == key) return &entry;
    return nullptr;
}

// Uploads the decoded RGBA image. Rows are top-down, which matches GL texture
// space, so the V coordinates line up with the sprite's own Y axis.
unsigned int GLTextureCache::upload(const std::filesystem::path& resolved, std::string& error) {
    const auto image = load_image(resolved);
    if (!image.valid()) {
        error = image.error.empty() ? "cannot decode the image" : image.error;
        return 0;
    }
    unsigned int id = 0;
    glGenTextures(1, &id);
    if (!id) {
        error = "the GPU refused a texture handle";
        return 0;
    }
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, image.pixels.data());
    if (glGetError() != GL_NO_ERROR) {
        glDeleteTextures(1, &id);
        error = "GPU rejected the texture data";
        return 0;
    }
    Entry entry;
    entry.key = resolved.string();
    entry.id = id;
    entry.width = image.width;
    entry.height = image.height;
    entries_.push_back(std::move(entry));
    return id;
}

unsigned int GLTextureCache::acquire(const std::string& texture_path,
                                     const std::filesystem::path& root, std::string& error) {
    if (texture_path.empty()) {
        error = "the sprite has no texture path";
        return 0;
    }
    std::error_code ec;
    // Project/repackage-relative paths are the contract ("assets/hero.png"),
    // but a bare file name is resolved against assets/ as a convenience so
    // hand-written scenes and imports still find their textures.
    const auto is_absolute_path = [](const std::string& p) {
        return p.size() >= 2 && std::isalpha(static_cast<unsigned char>(p[0])) && p[1] == ':';
    };
    std::vector<std::filesystem::path> candidates;
    candidates.push_back(std::filesystem::absolute(root / texture_path, ec).lexically_normal());
    if (!is_absolute_path(texture_path))
        candidates.push_back(std::filesystem::absolute(root / "assets" / texture_path, ec).lexically_normal());

    const std::string key = candidates.front().string();
    if (Entry* existing = find(key)) return existing->id;
    for (const auto& resolved : candidates) {
        std::string reason;
        if (const unsigned int id = upload(resolved, reason)) return id;
        if (error.empty()) error = reason;
    }
    const auto reason = key + ": " + error;
    if (std::find(failed_paths_.begin(), failed_paths_.end(), reason) == failed_paths_.end())
        failed_paths_.push_back(reason);
    return 0;
}

void GLTextureCache::draw(const Sprite2D& sprite, const Transform2D& world,
                          const std::filesystem::path& root) {
    if (!sprite.visible) return;

    std::string error;
    const unsigned int id = sprite.texture.empty() ? 0 : acquire(sprite.texture, root, error);
    const double half_w = 0.5 * sprite.size.x * world.scale.x;
    const double half_h = 0.5 * sprite.size.y * world.scale.y;
    const Vec2 corners[4] = {{-half_w, -half_h}, {half_w, -half_h}, {half_w, half_h}, {-half_w, half_h}};

    const std::uint8_t r = static_cast<std::uint8_t>((sprite.color >> 24) & 0xff);
    const std::uint8_t g = static_cast<std::uint8_t>((sprite.color >> 16) & 0xff);
    const std::uint8_t b = static_cast<std::uint8_t>((sprite.color >> 8) & 0xff);
    const std::uint8_t a = static_cast<std::uint8_t>(sprite.color & 0xff);
    glColor4ub(r, g, b, a);

    if (!id) {
        // No texture (or it failed to load): draw the tinted quad so the
        // sprite stays visible and debuggable either way.
        glBegin(GL_QUADS);
        for (int i = 0; i < 4; ++i) {
            const Vec2 p = rotate(corners[i], world.rotation);
            glVertex2d(world.position.x + p.x, world.position.y + p.y);
        }
        glEnd();
        return;
    }

    // Frame of a sliced frameset: the quad shows only the rect of the current
    // cell, so a sprite sheet animates without extra draw calls.
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
    if (sprite.has_frame()) {
        const Entry* entry = find(resolve_key(root, sprite.texture));
        if (entry && entry->width > 0 && entry->height > 0) {
            u0 = static_cast<float>(static_cast<double>(sprite.frame_x) / entry->width);
            v0 = static_cast<float>(static_cast<double>(sprite.frame_y) / entry->height);
            u1 = static_cast<float>(static_cast<double>(sprite.frame_x + sprite.frame_w) / entry->width);
            v1 = static_cast<float>(static_cast<double>(sprite.frame_y + sprite.frame_h) / entry->height);
        }
    }

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, id);
    glBegin(GL_QUADS);
    for (int i = 0; i < 4; ++i) {
        const Vec2 p = rotate(corners[i], world.rotation);
        const float u = (i == 1 || i == 2) ? u1 : u0;
        const float v = (i >= 2) ? v1 : v0;
        glTexCoord2f(u, v);
        glVertex2d(world.position.x + p.x, world.position.y + p.y);
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

std::pair<int, int> GLTextureCache::texture_size(const std::string& texture_path,
                                                 const std::filesystem::path& root) {
    std::string error;
    (void)acquire(texture_path, root, error);
    if (const Entry* entry = find(resolve_key(root, texture_path))) return {entry->width, entry->height};
    return {0, 0};
}

void GLTextureCache::draw_quad(double x, double y, double width, double height, std::uint32_t rgba) {
    const std::uint8_t r = static_cast<std::uint8_t>((rgba >> 24) & 0xff);
    const std::uint8_t g = static_cast<std::uint8_t>((rgba >> 16) & 0xff);
    const std::uint8_t b = static_cast<std::uint8_t>((rgba >> 8) & 0xff);
    const std::uint8_t a = static_cast<std::uint8_t>(rgba & 0xff);
    glColor4ub(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2d(x - width * 0.5, y - height * 0.5);
    glVertex2d(x + width * 0.5, y - height * 0.5);
    glVertex2d(x + width * 0.5, y + height * 0.5);
    glVertex2d(x - width * 0.5, y + height * 0.5);
    glEnd();
}

void GLTextureCache::reload(const std::string& texture_path, const std::filesystem::path& root) {
    const std::string key = resolve_key(root, texture_path);
    for (auto& entry : entries_) {
        if (entry.key != key) continue;
        if (entry.id) {
            glDeleteTextures(1, &entry.id);
            entry.id = 0;
        }
        std::string error;
        std::error_code ec;
        entry.id = upload(std::filesystem::absolute(root / texture_path, ec).lexically_normal(), error);
        return;
    }
}

} // namespace gridengine
