#include "SvgRenderer.h"

#include "SVGPreprocessor.h"
#include "lunasvg.h"

#include <mutex>

#pragma comment(lib, "lunasvg.lib")
#pragma comment(lib, "plutovg.lib")

struct SvgRenderer::Impl {
    std::unique_ptr<lunasvg::Document> document;
    float width = 0.0f;
    float height = 0.0f;
    mutable std::mutex renderMutex;
};

namespace {

SvgBitmap copyBitmapAsBgra(lunasvg::Bitmap& bitmap) {
    if (bitmap.isNull() || bitmap.width() <= 0 || bitmap.height() <= 0) {
        return {};
    }

    bitmap.convertToRGBA();

    SvgBitmap output;
    output.width = bitmap.width();
    output.height = bitmap.height();
    output.bgra.resize(static_cast<size_t>(output.width) * output.height * 4);

    const uint8_t* source = bitmap.data();
    for (int y = 0; y < output.height; ++y) {
        const uint8_t* sourceRow = source + static_cast<size_t>(y) * bitmap.stride();
        uint8_t* outputRow = output.bgra.data() + static_cast<size_t>(y) * output.width * 4;
        for (int x = 0; x < output.width; ++x) {
            outputRow[x * 4] = sourceRow[x * 4 + 2];
            outputRow[x * 4 + 1] = sourceRow[x * 4 + 1];
            outputRow[x * 4 + 2] = sourceRow[x * 4];
            outputRow[x * 4 + 3] = sourceRow[x * 4 + 3];
        }
    }

    return output;
}

}

SvgRenderer::SvgRenderer(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {
}

SvgRenderer::~SvgRenderer() = default;

std::string SvgRenderer::preprocess(std::span<const uint8_t> data) {
    if (data.empty()) {
        return {};
    }

    SVGPreprocessor preprocessor;
    auto processed = preprocessor.preprocessSVG(
        reinterpret_cast<const char*>(data.data()), data.size());
    if (!processed.empty()) {
        return processed;
    }

    return std::string(reinterpret_cast<const char*>(data.data()), data.size());
}

std::shared_ptr<SvgRenderer> SvgRenderer::create(std::span<const uint8_t> data) {
    auto source = preprocess(data);
    if (source.empty()) {
        return {};
    }

    auto document = lunasvg::Document::loadFromData(source.data(), source.size());
    if (!document || document->width() <= 0.0f || document->height() <= 0.0f) {
        return {};
    }

    auto impl = std::make_unique<Impl>();
    impl->width = document->width();
    impl->height = document->height();
    impl->document = std::move(document);
    return std::shared_ptr<SvgRenderer>(new SvgRenderer(std::move(impl)));
}

float SvgRenderer::width() const {
    return m_impl->width;
}

float SvgRenderer::height() const {
    return m_impl->height;
}

SvgBitmap SvgRenderer::renderToBitmap(int width, int height) const {
    if (width <= 0 || height <= 0) {
        return {};
    }

    std::scoped_lock lock(m_impl->renderMutex);
    auto bitmap = m_impl->document->renderToBitmap(width, height);
    return copyBitmapAsBgra(bitmap);
}

SvgBitmap SvgRenderer::renderViewport(int width, int height, const SvgTransform& transform) const {
    if (width <= 0 || height <= 0) {
        return {};
    }

    std::scoped_lock lock(m_impl->renderMutex);
    lunasvg::Bitmap bitmap(width, height);
    if (bitmap.isNull()) {
        return {};
    }

    bitmap.clear(0x00000000);
    const lunasvg::Matrix matrix(
        transform.a, transform.b, transform.c,
        transform.d, transform.e, transform.f);
    m_impl->document->render(bitmap, matrix);
    return copyBitmapAsBgra(bitmap);
}
