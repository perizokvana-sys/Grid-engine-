// GridEngine - include/gridengine/texture_gpu.hpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#pragma once
#include "gridengine/scene.hpp"
#include "gridengine/texture.hpp"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

// OpenGL 2D rendering for sprites and plain quads.
//
// Windows only for now (opengl32 + GL/gl.h). The caller owns the window and
// context, sets the orthographic projection and clears the frame; this helper
// only uploads textures and draws quads in that 2D space. Textures are cached
// per resolved path so repeated sprites upload once, and `reload()` supports
// hot-reload while editing.

namespace gridengine {

class GLTextureCache final {
public:
    GLTextureCache() = default;
    ~GLTextureCache();
    GLTextureCache(const GLTextureCache&) = delete;
    GLTextureCache& operator=(const GLTextureCache&) = delete;

    // Returns the OpenGL handle for a texture, loading it on first use.
    // On failure returns 0 and reports why in `error`.
    [[nodiscard]] unsigned int acquire(const std::string& texture_path,
                                       const std::filesystem::path& root,
                                       std::string& error);
    // Draws a textured sprite. Falls back to a solid tinted quad when the
    // texture is missing, so the caller never needs a separate branch. When the
    // sprite carries a resolved frameset frame, only that rect of the sheet is
    // drawn (sprite sheet animation).
    void draw(const Sprite2D& sprite, const Transform2D& world,
              const std::filesystem::path& root);
    // Solid quad, used by gizmos and missing-texture fallbacks.
    static void draw_quad(double x, double y, double width, double height, std::uint32_t rgba);
    // Pixel size of a texture, {0, 0} when it cannot be loaded. Used by the
    // frameset editor to slice a sheet into cells.
    [[nodiscard]] std::pair<int, int> texture_size(const std::string& texture_path,
                                                   const std::filesystem::path& root);
    // Re-uploads a texture from disk (used by the editor on request/project open).
    void reload(const std::string& texture_path, const std::filesystem::path& root);
    void clear();
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] std::size_t failed() const noexcept { return failed_paths_.size(); }
    [[nodiscard]] const std::vector<std::string>& failed_paths() const noexcept { return failed_paths_; }

private:
    struct Entry {
        std::string key;
        unsigned int id{0};
        int width{0};
        int height{0};
    };
    [[nodiscard]] unsigned int upload(const std::filesystem::path& resolved, std::string& error);
    Entry* find(const std::string& key);

    std::vector<Entry> entries_;
    std::vector<std::string> failed_paths_;
    std::filesystem::path root_;
};

} // namespace gridengine
