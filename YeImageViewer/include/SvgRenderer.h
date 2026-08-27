#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

struct SvgTransform {
    float a = 1.0f;
    float b = 0.0f;
    float c = 0.0f;
    float d = 1.0f;
    float e = 0.0f;
    float f = 0.0f;
};

struct SvgBitmap {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> bgra;

    bool empty() const {
        return width <= 0 || height <= 0 || bgra.empty();
    }
};

class SvgRenderer {
public:
    ~SvgRenderer();

    SvgRenderer(const SvgRenderer&) = delete;
    SvgRenderer& operator=(const SvgRenderer&) = delete;

    static std::shared_ptr<SvgRenderer> create(std::span<const uint8_t> data);
    static std::string preprocess(std::span<const uint8_t> data);

    float width() const;
    float height() const;
    SvgBitmap renderToBitmap(int width, int height) const;
    SvgBitmap renderViewport(int width, int height, const SvgTransform& transform) const;

private:
    struct Impl;
    explicit SvgRenderer(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};
