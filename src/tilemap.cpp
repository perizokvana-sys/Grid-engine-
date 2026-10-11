// GridEngine - src/tilemap.cpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#include "gridengine/tilemap.hpp"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <string>

namespace gridengine {
namespace {
constexpr std::uint64_t max_cells = 1'000'000;
bool valid_dimensions(std::uint32_t width, std::uint32_t height, double tile_width, double tile_height) {
    if (width == 0 || height == 0 || static_cast<std::uint64_t>(width) * height > max_cells ||
        !std::isfinite(tile_width) || !std::isfinite(tile_height) || tile_width <= 0 || tile_height <= 0) return false;
    // Keep the full map extent finite, so cell centers and world-space bounds
    // cannot overflow even when individual tile dimensions are finite.
    return std::isfinite(tile_width * static_cast<double>(width)) &&
           std::isfinite(tile_height * static_cast<double>(height));
}
}
TileMap2D::TileMap2D(std::uint32_t width, std::uint32_t height, double tile_width, double tile_height) {
    if (!valid_dimensions(width, height, tile_width, tile_height)) return;
    width_ = width; height_ = height; tile_width_ = tile_width; tile_height_ = tile_height;
    cells_.resize(static_cast<std::size_t>(width) * height);
}
bool TileMap2D::in_bounds(std::uint32_t x, std::uint32_t y) const noexcept { return x < width_ && y < height_; }
std::optional<TileCell> TileMap2D::cell(std::uint32_t x, std::uint32_t y) const noexcept {
    if (!in_bounds(x, y)) return {};
    return cells_[static_cast<std::size_t>(y) * width_ + x];
}
bool TileMap2D::set_cell(std::uint32_t x, std::uint32_t y, TileCell value) noexcept {
    if (!in_bounds(x, y)) return false;
    cells_[static_cast<std::size_t>(y) * width_ + x] = value;
    return true;
}
bool TileMap2D::clear_cell(std::uint32_t x, std::uint32_t y) noexcept { return set_cell(x, y, {}); }
Vec2 TileMap2D::cell_center(std::uint32_t x, std::uint32_t y) const noexcept {
    if (!in_bounds(x, y)) return {};
    return {(static_cast<double>(x) + .5) * tile_width_, (static_cast<double>(y) + .5) * tile_height_};
}
std::optional<std::pair<std::uint32_t, std::uint32_t>> TileMap2D::cell_at(Vec2 position) const noexcept {
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || position.x < 0 || position.y < 0 ||
        tile_width_ <= 0 || tile_height_ <= 0) return {};
    const double x = std::floor(position.x / tile_width_), y = std::floor(position.y / tile_height_);
    if (x >= width_ || y >= height_) return {};
    return std::pair{static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)};
}
std::size_t TileMap2D::tile_count() const noexcept {
    std::size_t count = 0;
    for (const auto& cell : cells_) if (cell.tile_id != 0) ++count;
    return count;
}
bool TileMap2D::save(const std::filesystem::path& path) const {
    if (empty()) return false;
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << "GridTileMap2D 1\n" << width_ << ' ' << height_ << ' ' << std::setprecision(17) << tile_width_ << ' ' << tile_height_ << '\n';
    for (const auto& cell : cells_) out << cell.tile_id << ' ' << cell.tint << '\n';
    return static_cast<bool>(out);
}
std::optional<TileMap2D> TileMap2D::load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::string signature;
    unsigned version{};
    std::uint32_t width{}, height{};
    double tile_width{}, tile_height{};
    if (!(in >> signature >> version) || signature != "GridTileMap2D" || version != 1 ||
        !(in >> width >> height >> tile_width >> tile_height) || !valid_dimensions(width, height, tile_width, tile_height)) return {};
    TileMap2D result(width, height, tile_width, tile_height);
    for (auto& cell : result.cells_) if (!(in >> cell.tile_id >> cell.tint)) return {};
    in >> std::ws;
    if (!in.eof()) return {};
    return result;
}
} // namespace gridengine
