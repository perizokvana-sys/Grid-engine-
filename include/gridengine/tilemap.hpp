// GridEngine - include/gridengine/tilemap.hpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#pragma once

#include "gridengine/scene.hpp"
#include <cstdint>
#include <filesystem>
#include <optional>
#include <utility>
#include <vector>

namespace gridengine {

struct TileCell { std::uint32_t tile_id{}; std::uint32_t tint{0xFFFFFFFFu}; };

// Fixed rectangular 2D grid. Tile ID zero represents an empty cell.
class TileMap2D final {
public:
    TileMap2D() = default;
    TileMap2D(std::uint32_t width, std::uint32_t height, double tile_width = 32.0, double tile_height = 32.0);
    [[nodiscard]] bool in_bounds(std::uint32_t x, std::uint32_t y) const noexcept;
    [[nodiscard]] std::optional<TileCell> cell(std::uint32_t x, std::uint32_t y) const noexcept;
    [[nodiscard]] bool set_cell(std::uint32_t x, std::uint32_t y, TileCell value) noexcept;
    [[nodiscard]] bool clear_cell(std::uint32_t x, std::uint32_t y) noexcept;
    [[nodiscard]] Vec2 cell_center(std::uint32_t x, std::uint32_t y) const noexcept;
    [[nodiscard]] std::optional<std::pair<std::uint32_t,std::uint32_t>> cell_at(Vec2 position) const noexcept;
    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    [[nodiscard]] double tile_width() const noexcept { return tile_width_; }
    [[nodiscard]] double tile_height() const noexcept { return tile_height_; }
    [[nodiscard]] std::size_t tile_count() const noexcept;
    [[nodiscard]] bool empty() const noexcept { return cells_.empty(); }
    [[nodiscard]] bool save(const std::filesystem::path& path) const;
    [[nodiscard]] static std::optional<TileMap2D> load(const std::filesystem::path& path);
private:
    std::uint32_t width_{}, height_{};
    double tile_width_{}, tile_height_{};
    std::vector<TileCell> cells_;
};

} // namespace gridengine
