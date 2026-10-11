// GridEngine - include/gridengine/texture.hpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#pragma once
#include "gridengine/scene.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// CPU-side image decoding. Pixels are stored as tightly packed RGBA8 rows
// (row-major, top-down, 4 bytes per pixel, format 0xRRGGBBAA).
//
// Decoders: BMP (own reader, cross-platform) and, on Windows, everything WIC
// can decode (PNG, JPEG, GIF, TIFF, ICO, WebP where the codec exists). No
// external image library is linked, and the decoded buffer is engine-owned, so
// the data can be uploaded to any renderer or sampled for queries.
//
// A load failure never throws: `error()` describes what went wrong (missing
// file, unreadable file, unsupported format, size limit) and the object stays
// empty.

namespace gridengine {

struct Image {
    std::vector<std::uint32_t> pixels; // RGBA8, tightly packed
    int width{0};
    int height{0};
    std::string error;
    std::filesystem::path source;

    [[nodiscard]] bool valid() const noexcept {
        return width > 0 && height > 0 && pixels.size() == static_cast<std::size_t>(width) *
                                                                 static_cast<std::size_t>(height);
    }
};

struct ImageLimits {
    int max_width{8192};
    int max_height{8192};
    std::size_t max_bytes{64u * 1024u * 1024u};
};

// Decodes an image file into RGBA8. `base` is an optional root directory for
// resolving relative paths (e.g. the project or package directory).
[[nodiscard]] Image load_image(const std::filesystem::path& file,
                               const std::filesystem::path& base = {},
                               const ImageLimits& limits = {});
[[nodiscard]] bool save_image_bmp(const std::filesystem::path& file, const Image& image);

} // namespace gridengine
