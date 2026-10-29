#include "PresentationImage_p.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#endif
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_MAX_DIMENSIONS 16384
#include "stb/stb_image.h"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb/stb_image_write.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
namespace iiFileProvider::native::detail {
std::optional<FilePreview> imagePreview(std::string_view input, FileDetails &details) {
    if (input.empty() || input.size() > Presentation::maximumSourceBytes) return {};
    const auto *data = reinterpret_cast<const stbi_uc *>(input.data());
    int width = 0, height = 0, components = 0;
    if (!stbi_info_from_memory(data, static_cast<int>(input.size()), &width, &height, &components)
        || width <= 0 || height <= 0) return {};
    details.properties["width"] = std::to_string(width);
    details.properties["height"] = std::to_string(height);
    details.properties["channels"] = std::to_string(components);
    // Inspect before allocation, including small files declaring huge rasters.
    if (std::uint64_t(width) * height > 16ull * 1024 * 1024) return {};
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(data, static_cast<int>(input.size()), &width, &height, &components, 4), stbi_image_free);
    if (!pixels) return {};
    const double scale = std::min({1.0, 512.0 / width, 512.0 / height});
    const int outWidth = std::max(1, int(width * scale)), outHeight = std::max(1, int(height * scale));
    std::vector<unsigned char> output(std::size_t(outWidth) * outHeight * 4);
    // Area averaging retains thin lines when a large image is reduced.
    for (int y = 0; y < outHeight; ++y) for (int x = 0; x < outWidth; ++x) {
        const int x0 = x * width / outWidth, x1 = std::max(x0 + 1, (x + 1) * width / outWidth);
        const int y0 = y * height / outHeight, y1 = std::max(y0 + 1, (y + 1) * height / outHeight);
        std::uint64_t sums[4]{};
        for (int sy = y0; sy < y1; ++sy) for (int sx = x0; sx < x1; ++sx)
            for (int c = 0; c < 4; ++c) sums[c] += pixels.get()[(std::size_t(sy) * width + sx) * 4 + c];
        const auto count = std::uint64_t(x1 - x0) * (y1 - y0);
        for (int c = 0; c < 4; ++c) output[(std::size_t(y) * outWidth + x) * 4 + c] = static_cast<unsigned char>(sums[c] / count);
    }
    FilePreview preview{"image/png", "image", {}, unsigned(outWidth), unsigned(outHeight)};
    struct Output { std::string bytes; bool failed = false; } encoded;
    const auto append = [](void *context, void *bytes, int length) {
        auto &target = *static_cast<Output *>(context);
        if (target.failed) return;
        if (target.bytes.size() + std::size_t(length) > Presentation::maximumDocumentBytes / 2) { target.failed = true; return; }
        try { target.bytes.append(static_cast<const char *>(bytes), static_cast<std::size_t>(length)); }
        catch (...) { target.failed = true; } // Do not unwind through stb's raw allocations.
    };
    if (!stbi_write_png_to_func(append, &encoded, outWidth, outHeight, 4, output.data(), outWidth * 4) || encoded.failed) return {};
    preview.bytes = std::move(encoded.bytes);
    return preview;
}
}
