// GridEngine - src/texture.cpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#include "gridengine/texture.hpp"

#include <cctype>
#include <cstring>
#include <fstream>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincodec.h>
#endif

namespace gridengine {
namespace {

Image reject(const std::filesystem::path& source, const std::string& message) {
    Image image;
    image.source = source;
    image.error = message;
    return image;
}

bool read_file(const std::filesystem::path& path, std::vector<std::uint8_t>& out, std::string& error) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) { error = "file does not exist"; return false; }
    std::ifstream in(path, std::ios::binary);
    if (!in) { error = "cannot open the file"; return false; }
    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    in.seekg(0, std::ios::beg);
    if (size <= 0 || size > static_cast<std::streamoff>(256u * 1024u * 1024u)) {
        error = "file has an unexpected size";
        return false;
    }
    out.resize(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(out.data()), size);
    if (!in) { error = "cannot read the file"; return false; }
    return true;
}

std::uint32_t read_le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
std::uint16_t read_le16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[0]) |
                                      (static_cast<std::uint16_t>(p[1]) << 8));
}

// Uncompressed, bottom-up, 24/32-bit BMP. Enough for editor-generated art and
// keeps the engine independent of WIC for the simplest format.
Image decode_bmp(const std::vector<std::uint8_t>& data, const std::filesystem::path& source,
                 const ImageLimits& limits) {
    if (data.size() < 54) return reject(source, "BMP file is truncated");
    if (data[0] != 'B' || data[1] != 'M') return reject(source, "not a BMP file");
    const auto pixel_offset = read_le32(data.data() + 10);
    const int width = static_cast<int>(read_le32(data.data() + 18));
    // Positive height = bottom-up; negative = top-down.
    const auto header_height = static_cast<std::int32_t>(read_le32(data.data() + 22));
    const bool top_down = header_height < 0;
    const int height = top_down ? -header_height : header_height;
    const int bpp = read_le16(data.data() + 28);
    const auto compression = read_le32(data.data() + 30);
    if (compression != 0) return reject(source, "compressed BMP is not supported");
    if (width <= 0 || height <= 0) return reject(source, "BMP has no pixels");
    if (width > limits.max_width || height > limits.max_height) return reject(source, "BMP exceeds the size limit");
    const int channels = bpp / 8;
    if (channels != 3 && channels != 4)
        return reject(source, "only 24 and 32 bit BMP is supported");
    const std::size_t row_bytes = static_cast<std::size_t>(width) * channels;
    const std::size_t stride = (row_bytes + 3u) & ~static_cast<std::size_t>(3u);
    if (pixel_offset + stride > data.size() ||
        pixel_offset + stride * static_cast<std::size_t>(height) > data.size() + stride)
        return reject(source, "BMP pixel data is truncated");

    Image image;
    image.source = source;
    image.width = width;
    image.height = height;
    image.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    const std::uint8_t* pixels = data.data() + pixel_offset;
    for (int y = 0; y < height; ++y) {
        const int source_row = top_down ? y : height - 1 - y;
        const std::uint8_t* row = pixels + stride * static_cast<std::size_t>(source_row);
        for (int x = 0; x < width; ++x) {
            const std::uint8_t* p = row + static_cast<std::size_t>(x) * channels;
            const std::uint32_t r = p[2];
            const std::uint32_t g = p[1];
            const std::uint32_t b = p[0];
            const std::uint32_t a = channels == 4 ? p[3] : 0xffu;
            image.pixels[static_cast<std::size_t>(y) * width + x] = (r << 24) | (g << 16) | (b << 8) | a;
        }
    }
    return image;
}

#ifdef _WIN32
class WicScope {
public:
    WicScope() {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (hr == RPC_E_CHANGED_MODE) hr = S_OK;
        initialized_ = SUCCEEDED(hr);
        if (initialized_)
            initialized_ = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                                     IID_PPV_ARGS(&factory_)));
    }
    ~WicScope() {
        if (factory_) factory_->Release();
        if (initialized_) CoUninitialize();
    }
    [[nodiscard]] IWICImagingFactory* factory() const { return factory_; }

private:
    IWICImagingFactory* factory_{nullptr};
    bool initialized_{false};
};

// Decodes any WIC-supported format (PNG, JPEG, GIF, TIFF, ICO) into RGBA8.
Image decode_wic(const std::filesystem::path& path, const ImageLimits& limits) {
    WicScope wic;
    if (!wic.factory()) return reject(path, "cannot initialize the Windows imaging factory");
    IWICBitmapDecoder* decoder = nullptr;
    if (FAILED(wic.factory()->CreateDecoderFromFilename(path.wstring().c_str(), nullptr, GENERIC_READ,
                                                        WICDecodeMetadataCacheOnDemand, &decoder)) || !decoder)
        return reject(path, "unsupported or unreadable image format (Windows Imaging)");
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    Image image;
    image.source = path;
    auto release = [&] {
        if (converter) converter->Release();
        if (frame) frame->Release();
        if (decoder) decoder->Release();
    };
    do {
        if (FAILED(decoder->GetFrame(0, &frame)) || !frame) {
            image.error = "the image has no readable frame";
            break;
        }
        UINT width = 0, height = 0;
        if (FAILED(frame->GetSize(&width, &height)) || width == 0 || height == 0) {
            image.error = "the image has no pixels";
            break;
        }
        if (width > static_cast<UINT>(limits.max_width) || height > static_cast<UINT>(limits.max_height)) {
            image.error = "the image exceeds the size limit";
            break;
        }
        if (FAILED(wic.factory()->CreateFormatConverter(&converter)) || !converter) {
            image.error = "cannot convert the image format";
            break;
        }
        if (FAILED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                                        nullptr, 0.0, WICBitmapPaletteTypeMedianCut)) || !converter) {
            image.error = "cannot convert the image to RGBA";
            break;
        }
        image.width = static_cast<int>(width);
        image.height = static_cast<int>(height);
        image.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
        if (FAILED(converter->CopyPixels(nullptr, static_cast<UINT>(width * 4u),
                                        static_cast<UINT>(image.pixels.size() * 4u),
                                        reinterpret_cast<BYTE*>(image.pixels.data())))) {
            image.error = "cannot copy the image pixels";
            image.pixels.clear();
            break;
        }
        // CopyPixels writes raw R,G,B,A bytes into the buffer, while the engine
        // stores packed RGBA pixels (red in the most significant byte). On a
        // little-endian host those two views differ, so pack the bytes here.
        {
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(image.pixels.data());
            for (std::size_t i = 0; i < image.pixels.size(); ++i) {
                const std::uint8_t* p = bytes + i * 4u;
                image.pixels[i] = (static_cast<std::uint32_t>(p[0]) << 24) |
                                  (static_cast<std::uint32_t>(p[1]) << 16) |
                                  (static_cast<std::uint32_t>(p[2]) << 8) |
                                  static_cast<std::uint32_t>(p[3]);
            }
        }
    } while (false);
    release();
    if (!image.error.empty()) return {std::move(image)};
    if (!image.valid()) {
        image.error = "decoded image has an unexpected size";
        return image;
    }
    return image;
}
#else
Image decode_wic(const std::filesystem::path& path, const ImageLimits& limits) {
    (void)limits;
    return reject(path, "only BMP is supported on this platform");
}
#endif

} // namespace

Image load_image(const std::filesystem::path& file, const std::filesystem::path& base,
                const ImageLimits& limits) {
    std::filesystem::path resolved = file.is_absolute() ? file : base / file;
    std::error_code ec;
    resolved = std::filesystem::weakly_canonical(resolved, ec);
    std::vector<std::uint8_t> data;
    std::string error;
    if (!read_file(resolved, data, error)) return reject(resolved, error);
    const std::size_t limit = limits.max_bytes;
    if (data.size() > limit) return reject(resolved, "the file exceeds the byte limit");

    std::string extension = resolved.has_extension() ? resolved.extension().string() : std::string{};
    for (auto& c : extension) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (extension == ".bmp") return decode_bmp(data, resolved, limits);
    if (extension.empty()) {
        // Try BMP first (own reader), then fall back to the platform decoder.
        if (data.size() >= 2 && data[0] == 'B' && data[1] == 'M') return decode_bmp(data, resolved, limits);
        return decode_wic(resolved, limits);
    }
    if (extension == ".dds" || extension == ".ktx")
        return reject(resolved, "compressed texture formats are not supported (use PNG or BMP)");
    return decode_wic(resolved, limits);
}

bool save_image_bmp(const std::filesystem::path& file, const Image& image) {
    if (!image.valid()) return false;
    const int width = image.width;
    const int height = image.height;
    const std::size_t row_bytes = static_cast<std::size_t>(width) * 3u;
    const std::size_t stride = (row_bytes + 3u) & ~static_cast<std::size_t>(3u);
    const std::size_t pixels_bytes = stride * static_cast<std::size_t>(height);
    const std::size_t header_size = 54u;
    std::vector<std::uint8_t> data(header_size + pixels_bytes, 0);
    data[0] = 'B';
    data[1] = 'M';
    const auto file_size = static_cast<std::uint32_t>(data.size());
    data[2] = static_cast<std::uint8_t>(file_size);
    data[3] = static_cast<std::uint8_t>(file_size >> 8);
    data[4] = static_cast<std::uint8_t>(file_size >> 16);
    data[5] = static_cast<std::uint8_t>(file_size >> 24);
    data[10] = static_cast<std::uint8_t>(header_size);
    // DIB header.
    data[14] = 40;
    data[18] = static_cast<std::uint8_t>(width);
    data[19] = static_cast<std::uint8_t>(width >> 8);
    data[20] = static_cast<std::uint8_t>(width >> 16);
    data[21] = static_cast<std::uint8_t>(width >> 24);
    data[22] = static_cast<std::uint8_t>(height);
    data[23] = static_cast<std::uint8_t>(height >> 8);
    data[24] = static_cast<std::uint8_t>(height >> 16);
    data[25] = static_cast<std::uint8_t>(height >> 24);
    data[26] = 1;
    data[28] = 24;
    for (int y = 0; y < height; ++y) {
        const int source_row = height - 1 - y; // BMP is bottom-up
        std::uint8_t* row = data.data() + header_size + stride * static_cast<std::size_t>(y);
        for (int x = 0; x < width; ++x) {
            const std::uint32_t rgba = image.pixels[static_cast<std::size_t>(source_row) * width + x];
            std::uint8_t* p = row + static_cast<std::size_t>(x) * 3u;
            p[0] = static_cast<std::uint8_t>(rgba >> 8);  // blue
            p[1] = static_cast<std::uint8_t>(rgba >> 16); // green
            p[2] = static_cast<std::uint8_t>(rgba >> 24); // red
        }
    }
    std::ofstream out(file, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(out);
}

} // namespace gridengine
