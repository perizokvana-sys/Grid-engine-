// GridEngine - tests/tilemap.cpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#include "gridengine/tilemap.hpp"
#include <cassert>
#include <cmath>
#include <fstream>
#include <limits>
#include <filesystem>
using namespace gridengine;

int main() {
    // Basic construction and bounds.
    TileMap2D map(4, 3, 32.0, 64.0);
    assert(!map.empty());
    assert(map.width() == 4 && map.height() == 3);
    assert(map.tile_width() == 32.0 && map.tile_height() == 64.0);
    assert(map.in_bounds(0, 0) && map.in_bounds(3, 2));
    assert(!map.in_bounds(4, 0) && !map.in_bounds(0, 3));

    // Empty cells read as default (tile_id 0).
    auto empty = map.cell(1, 1);
    assert(empty && empty->tile_id == 0);

    // Set / get / clear.
    assert(map.set_cell(1, 1, {7, 0xFF00FF00u}));
    auto c = map.cell(1, 1);
    assert(c && c->tile_id == 7 && c->tint == 0xFF00FF00u);
    assert(!map.set_cell(4, 0, {1, 0})); // out of bounds rejected
    assert(!map.set_cell(0, 3, {1, 0}));
    assert(map.clear_cell(1, 1));
    assert(map.cell(1, 1)->tile_id == 0);
    assert(!map.clear_cell(99, 99));

    // tile_count counts non-empty cells.
    assert(map.tile_count() == 0);
    map.set_cell(0, 0, {1, 0});
    map.set_cell(2, 2, {1, 0});
    assert(map.tile_count() == 2);

    // cell_center: world position at the middle of a cell.
    auto center = map.cell_center(0, 0);
    assert(std::abs(center.x - 16.0) < 1e-9 && std::abs(center.y - 32.0) < 1e-9);
    auto center2 = map.cell_center(3, 2);
    assert(std::abs(center2.x - 112.0) < 1e-9 && std::abs(center2.y - 160.0) < 1e-9);

    // cell_at: world -> grid conversion, including negative coordinates.
    auto at = map.cell_at({16.0, 32.0});
    assert(at && at->first == 0 && at->second == 0);
    at = map.cell_at({112.0, 160.0});
    assert(at && at->first == 3 && at->second == 2);
    at = map.cell_at({-1.0, 0.0});
    assert(!at); // negative coordinates are outside
    at = map.cell_at({200.0, 0.0});
    assert(!at); // beyond the map
    at = map.cell_at({std::numeric_limits<double>::quiet_NaN(), 0.0});
    assert(!at);
    at = map.cell_at({0.0, std::numeric_limits<double>::infinity()});
    assert(!at);

    // Zero-dimension map is empty and rejects everything.
    TileMap2D empty_map(0, 0);
    assert(empty_map.empty());
    assert(!empty_map.in_bounds(0, 0));
    assert(!empty_map.set_cell(0, 0, {1, 0}));
    assert(!empty_map.save("should_not_write.gridmap"));

    // Constructor rejects non-finite or non-positive tile sizes / invalid dims.
    TileMap2D bad1(4, 3, 0.0, 32.0);
    assert(bad1.empty());
    TileMap2D bad2(4, 3, 32.0, std::numeric_limits<double>::infinity());
    assert(bad2.empty());
    TileMap2D bad3(4, 3, -32.0, 32.0);
    assert(bad3.empty());
    TileMap2D bad4(0, 3, 32.0, 32.0);
    assert(bad4.empty());

    // Overflow guard: finite tile sizes whose product with the grid dimensions
    // overflows to infinity. Cell count stays within max_cells, so this
    // isolates the extent-overflow check.
    const double huge = 1e308;
    TileMap2D overflow_map(2, 2, huge, huge);
    assert(overflow_map.empty());

    // Save / load round-trip.
    TileMap2D save_map(2, 2, 16.0, 16.0);
    save_map.set_cell(0, 0, {5, 0x11223344u});
    save_map.set_cell(1, 1, {9, 0xAABBCCDDu});
    const auto path = std::filesystem::temp_directory_path() / "gridengine_tilemap_test.gridmap";
    assert(save_map.save(path));
    auto loaded = TileMap2D::load(path);
    assert(loaded);
    assert(loaded->width() == 2 && loaded->height() == 2);
    assert(loaded->cell(0, 0)->tile_id == 5 && loaded->cell(0, 0)->tint == 0x11223344u);
    assert(loaded->cell(1, 1)->tile_id == 9 && loaded->cell(1, 1)->tint == 0xAABBCCDDu);
    assert(loaded->cell(1, 0)->tile_id == 0);
    std::error_code ec;
    std::filesystem::remove(path, ec);

    // Corrupt load fails gracefully.
    const auto corrupt = std::filesystem::temp_directory_path() / "gridengine_tilemap_corrupt.gridmap";
    { std::ofstream out(corrupt, std::ios::binary); out << "GridTileMap2D 1\n4 3 32 32\n1 4294967295\n"; }
    assert(!TileMap2D::load(corrupt)); // missing trailing cell data
    std::filesystem::remove(corrupt, ec);

    return 0;
}
