#include "StbImageDecoder.h"

#ifndef STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#endif

#include <climits>
#include <limits>
#include <memory>

StbImageDecoder::Image StbImageDecoder::decode(std::span<const uint8_t> buffer) {
    if (buffer.empty() || buffer.size() > INT_MAX) {
        return {};
    }

    int width = 0;
    int height = 0;
    int originalChannels = 0;
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> rgba{
        stbi_load_from_memory(buffer.data(), static_cast<int>(buffer.size()), &width, &height, &originalChannels, STBI_rgb_alpha),
        stbi_image_free
    };
    if (!rgba || width <= 0 || height <= 0) {
        return {};
    }

    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (pixelCount > std::numeric_limits<size_t>::max() / 4) {
        return {};
    }

    Image image;
    image.width = width;
    image.height = height;
    image.bgra.resize(pixelCount * 4);

    for (size_t i = 0; i < pixelCount; ++i) {
        image.bgra[i * 4] = rgba.get()[i * 4 + 2];
        image.bgra[i * 4 + 1] = rgba.get()[i * 4 + 1];
        image.bgra[i * 4 + 2] = rgba.get()[i * 4];
        image.bgra[i * 4 + 3] = rgba.get()[i * 4 + 3];
    }

    return image;
}
