// Texture decoding tests.
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.
//
// The PNG case decodes a real PNG file: the test writes one with a minimal
// encoder (zlib stored blocks + adler32/chunk CRC32, no external library), so
// decoding goes through the same Windows Imaging path the editor and the
// exported game use. The BMP case exercises the built-in decoder. Failures
// (missing file, truncated data, unsupported format) must be reported, not
// crash or return a half-decoded image.

#include "gridengine/texture.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

using gridengine::Image;

namespace {

std::uint32_t crc32_of(const std::vector<std::uint8_t>& data) {
    static std::uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
            table[n] = c;
        }
        ready = true;
    }
    std::uint32_t c = 0xffffffffu;
    for (const auto byte : data) c = table[(c ^ byte) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}
void push_be32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 24));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}
void push_chunk(std::vector<std::uint8_t>& out, const char* type, const std::vector<std::uint8_t>& payload) {
    push_be32(out, static_cast<std::uint32_t>(payload.size()));
    const std::size_t start = out.size();
    for (const char* p = type; *p; ++p) out.push_back(static_cast<std::uint8_t>(*p));
    out.insert(out.end(), payload.begin(), payload.end());
    const std::uint32_t crc = crc32_of(std::vector<std::uint8_t>(out.begin() + start, out.end()));
    push_be32(out, crc);
}

// Writes an RGB PNG with zlib "stored" blocks (no compression) plus one filter
// byte per scanline. Deterministic and dependency-free.
bool write_png(const std::filesystem::path& file, int width, int height,
               const std::vector<std::uint32_t>& pixels) {
    if (width <= 0 || height <= 0) return false;
    std::vector<std::uint8_t> raw;
    for (int y = 0; y < height; ++y) {
        raw.push_back(0x00); // filter: none
        for (int x = 0; x < width; ++x) {
            const std::uint32_t rgba = pixels[static_cast<std::size_t>(y) * width + x];
            raw.push_back(static_cast<std::uint8_t>((rgba >> 24) & 0xff));
            raw.push_back(static_cast<std::uint8_t>((rgba >> 16) & 0xff));
            raw.push_back(static_cast<std::uint8_t>((rgba >> 8) & 0xff));
        }
    }
    std::uint32_t a = 1, b = 0;
    for (const auto byte : raw) {
        a = (a + byte) % 65521u;
        b = (b + a) % 65521u;
    }
    std::vector<std::uint8_t> zlib{0x78, 0x01}; // zlib header, no preset dictionary
    std::size_t offset = 0;
    do {
        const std::size_t remaining = raw.size() - offset;
        const std::size_t block = remaining > 65535u ? 65535u : remaining;
        const bool final = (offset + block == raw.size());
        zlib.push_back(final ? 0x01 : 0x00);
        zlib.push_back(static_cast<std::uint8_t>(block & 0xff));
        zlib.push_back(static_cast<std::uint8_t>(block >> 8));
        const auto nlen = static_cast<std::uint16_t>(~static_cast<std::uint16_t>(block));
        zlib.push_back(static_cast<std::uint8_t>(nlen & 0xff));
        zlib.push_back(static_cast<std::uint8_t>(nlen >> 8));
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                    raw.begin() + static_cast<std::ptrdiff_t>(offset + block));
        offset += block;
    } while (offset < raw.size());
    push_be32(zlib, (b << 16) | a); // adler32, big endian

    std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    std::vector<std::uint8_t> ihdr;
    push_be32(ihdr, static_cast<std::uint32_t>(width));
    push_be32(ihdr, static_cast<std::uint32_t>(height));
    ihdr.push_back(8); // bit depth
    ihdr.push_back(2); // color type: truecolor RGB
    ihdr.push_back(0); // compression
    ihdr.push_back(0); // filter
    ihdr.push_back(0); // interlace
    push_chunk(png, "IHDR", ihdr);
    push_chunk(png, "IDAT", zlib);
    push_chunk(png, "IEND", {});
    std::ofstream out(file, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return static_cast<bool>(out);
}

int failures = 0;
void check(bool ok, const char* what) {
    std::printf("  %-60s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) ++failures;
}

std::uint32_t rgba(int r, int g, int b, int a) {
    return (static_cast<std::uint32_t>(r) << 24) | (static_cast<std::uint32_t>(g) << 16) |
           (static_cast<std::uint32_t>(b) << 8) | static_cast<std::uint32_t>(a);
}

} // namespace

int main() {
    const auto dir = std::filesystem::temp_directory_path() / "gridengine_texture_test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    // --- PNG through the platform decoder ---------------------------------
    std::vector<std::uint32_t> png_pixels = {rgba(255, 0, 0, 255), rgba(0, 255, 0, 255), rgba(0, 0, 255, 255),
                                             rgba(255, 255, 255, 128)};
    check(write_png(dir / "test.png", 2, 2, png_pixels), "write a real 2x2 PNG");
    // The PNG decoder is the platform one (WIC on Windows); elsewhere the
    // engine answers "only BMP is supported on this platform", so the checks
    // that need a real decode only run where such a decoder exists.
#ifdef _WIN32
    {
        const Image png = gridengine::load_image(dir / "test.png");
        check(png.valid(), "decode the PNG");
        check(png.width == 2 && png.height == 2, "PNG dimensions");
        for (int i = 0; png.valid() && i < 4; ++i)
            std::printf("  decoded pixel %d = %08X (want %08X)\n", i, png.pixels[i], png_pixels[i]);
        // The source PNG has no alpha, so every decoded pixel is opaque.
        check(png.valid() && png.pixels[0] == rgba(255, 0, 0, 255) &&
                  png.pixels[1] == rgba(0, 255, 0, 255) && png.pixels[2] == rgba(0, 0, 255, 255) &&
                  png.pixels[3] == rgba(255, 255, 255, 255),
              "PNG pixel values (alpha normalized to opaque)");
    }
#else
    std::printf("  PNG decoding skipped (the platform decoder is Windows-only)\n");
#endif

    // --- BMP through the built-in decoder --------------------------------
    {
        Image source;
        source.width = 3;
        source.height = 2;
        source.pixels = {rgba(10, 20, 30, 255), rgba(40, 50, 60, 255), rgba(70, 80, 90, 255),
                         rgba(100, 110, 120, 255), rgba(130, 140, 150, 255), rgba(160, 170, 180, 255)};
        const auto bmp = dir / "test.bmp";
        check(gridengine::save_image_bmp(bmp, source), "write a 3x2 BMP");
        const Image decoded = gridengine::load_image(bmp);
        check(decoded.valid() && decoded.width == 3 && decoded.height == 2, "decode the BMP");
        bool same = decoded.valid();
        for (int i = 0; same && i < 6; ++i) same = decoded.pixels[i] == source.pixels[i];
        check(same, "BMP pixel values round-trip (bottom-up, padded rows)");
    }

    // --- relative path resolution -----------------------------------------
    // Uses the BMP written above: the check is about path resolution, and BMP
    // decoding works on every platform.
    {
        const auto nested = dir / "nested" / "test.bmp";
        std::filesystem::create_directories(nested.parent_path(), ec);
        std::filesystem::copy_file(dir / "test.bmp", nested, ec);
        const Image image = gridengine::load_image("nested/test.bmp", dir);
        check(image.valid() && image.width == 3, "relative path resolves against the base root");
    }

    // --- failures ----------------------------------------------------------
    {
        const Image missing = gridengine::load_image(dir / "does-not-exist.png");
        check(!missing.valid() && missing.error == "file does not exist", "missing file reports a clear error");
    }
    {
        std::ofstream truncate(dir / "broken.png", std::ios::binary);
        const auto bytes = [&] {
            std::ifstream in(dir / "test.png", std::ios::binary);
            return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), {});
        }();
        truncate.write(reinterpret_cast<const char*>(bytes.data()), bytes.size() / 2);
        truncate.close();
        const Image broken = gridengine::load_image(dir / "broken.png");
        check(!broken.valid() && !broken.error.empty(), "truncated PNG is rejected with a message");
    }
    {
        std::ofstream garbage(dir / "garbage.png", std::ios::binary);
        garbage << "this is definitely not an image";
        garbage.close();
        const Image garbage_image = gridengine::load_image(dir / "garbage.png");
        check(!garbage_image.valid() && !garbage_image.error.empty(), "garbage bytes are rejected");
    }
    {
        std::ofstream dds(dir / "fake.dds", std::ios::binary);
        dds << "DDS ";
        dds.close();
        const Image dds_image = gridengine::load_image(dir / "fake.dds");
        check(!dds_image.valid(), "compressed texture formats are refused explicitly");
    }

    // Keep the generated files for debugging with GRIDENGINE_TEXTURE_KEEP=1.
    const char* keep = std::getenv("GRIDENGINE_TEXTURE_KEEP");
    if (!keep || keep[0] != '1') std::filesystem::remove_all(dir, ec);
    std::printf("gridengine_texture: %s (%d failures)\n", failures == 0 ? "ok" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
