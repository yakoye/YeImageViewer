#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace StbImageDecoder {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> bgra;
};

Image decode(std::span<const uint8_t> buffer);

}
