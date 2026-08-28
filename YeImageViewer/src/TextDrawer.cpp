#pragma once
#include "TextDrawer.h"

// https://github.com/nothings/stb
// 整个工程只能一个源文件定义 STB_TRUETYPE_IMPLEMENTATION， 其他地方只需include
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

void TextDrawer::setLineGap(float percent) {
    lineGapPercent = percent;
}

void TextDrawer::setSize(int newSize) {
    fontSize = newSize > 2048 ? 2048 : (newSize < 12 ? 12 : newSize);
    scale = 0;

    auto newBufferSize = 2ULL * fontSize * fontSize;
    wordBuff.resize(newBufferSize);
    memset(wordBuff.data(), 0, newBufferSize);
    asciiCache.clear();
    asciiCache.resize(256);
}

// str : UTF-8
void TextDrawer::putText(cv::Mat& img, const int x, const int y, const char* str, intUnion color,
    bool isAdaptiveFG, bool enhanceGlyphCoverage) {
    if (TextRenderingPolicy::usesNativeClearType(isAdaptiveFG, enhanceGlyphCoverage)) {
        drawNativeText(img, { x, y, img.cols - x, img.rows - y }, str, color,
            DT_LEFT | DT_TOP | DT_NOPREFIX);
        return;
    }

    if (!hasInit) {
        Init(IDR_TTF_DEFAULT, L"TTF");
        hasInit = true;
    }
    if (scale == 0)
        scale = stbtt_ScaleForPixelHeight(&info, (float)fontSize);

    int codePoint = '?';
    int xOffset = x, yOffset = y;
    const auto len = strlen(str);
    size_t i = 0;
    while (i < len)
    {
        if ((str[i] & 0x80) == 0) {
            codePoint = str[i];
            i++;
        }
        else if ((str[i] & 0xe0) == 0xc0) { // 110x'xxxx 10xx'xxxx
            codePoint = ((str[i] & 0x1f) << 6) | (str[i + 1] & 0x3f);
            i += 2;
        }
        else if ((str[i] & 0xf0) == 0xe0) { // 1110'xxxx 10xx'xxxx 10xx'xxxx
            codePoint = ((str[i] & 0x0f) << 12) | ((str[i + 1] & 0x3f) << 6) | (str[i + 2] & 0x3f);
            i += 3;
        }
        else if ((str[i] & 0xf8) == 0xf0) { // 1111'0xxx 10xx'xxxx 10xx'xxxx 10xx'xxxx
            codePoint = ((str[i] & 0x07) << 18) | ((str[i + 1] & 0x3f) << 12) | ((str[i + 2] & 0x3f) << 6) | (str[i + 3] & 0x3f);
            i += 4;
        }
        else {
            codePoint = '?';
            i++;
        }

        if (codePoint == '\n') {
            yOffset += int(fontSize * (1 + lineGapPercent));
            xOffset = x;
        }
        else {
            xOffset += putWord(img, xOffset, yOffset, codePoint, color,
                isAdaptiveFG, enhanceGlyphCoverage);
        }
    }
}

//Rect {x, y, width, height}
void TextDrawer::putAlignCenter(cv::Mat& img, cv::Rect rect, const char* str, intUnion color,
    bool isAdaptiveFG, bool enhanceGlyphCoverage) {
    if (TextRenderingPolicy::usesNativeClearType(isAdaptiveFG, enhanceGlyphCoverage)) {
        drawNativeText(img, rect, str, color,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        return;
    }

    int codePoint = '?';
    int H = 1, W = 0, W_cnt = 0;
    const auto len = strlen(str);
    size_t i = 0;
    while (i < len) {
        if ((str[i] & 0x80) == 0) {
            codePoint = str[i];
            i++;
        }
        else if ((str[i] & 0xe0) == 0xc0) { // 110x'xxxx 10xx'xxxx
            codePoint = ((str[i] & 0x1f) << 6) | (str[i + 1] & 0x3f);
            i += 2;
        }
        else if ((str[i] & 0xf0) == 0xe0) { // 1110'xxxx 10xx'xxxx 10xx'xxxx
            codePoint = ((str[i] & 0x0f) << 12) | ((str[i + 1] & 0x3f) << 6) | (str[i + 2] & 0x3f);
            i += 3;
        }
        else if ((str[i] & 0xf8) == 0xf0) { // 1111'0xxx 10xx'xxxx 10xx'xxxx 10xx'xxxx
            codePoint = ((str[i] & 0x07) << 18) | ((str[i + 1] & 0x3f) << 12) | ((str[i + 2] & 0x3f) << 6) | (str[i + 3] & 0x3f);
            i += 4;
        }
        else {
            codePoint = '?';
            i++;
        }

        if (codePoint == '\n') {
            H++;
            if (W_cnt > W) {
                W = W_cnt;
                W_cnt = 0;
            }
        }
        else {
            W_cnt += (codePoint < 256 ? 1 : 2);
        }
    }

    if (W_cnt > W)
        W = W_cnt;

    const int sizeAndGap = int(fontSize * (1 + lineGapPercent));// Mono Font
    H *= sizeAndGap;
    W = sizeAndGap * W / 2;

    const int x = rect.x + (rect.width - W) / 2;
    const int y = rect.y + (rect.height - H) / 2;

    putText(img, x, y, str, color, isAdaptiveFG, enhanceGlyphCoverage);
}

//Rect {x, y, width, height}
void TextDrawer::putAlignLeft(cv::Mat& img, cv::Rect rect, const char* str, intUnion color,
    bool isAdaptiveFG, bool enhanceGlyphCoverage) {
    if (TextRenderingPolicy::usesNativeClearType(isAdaptiveFG, enhanceGlyphCoverage)) {
        const bool multiline = strchr(str, '\n') != nullptr;
        drawNativeText(img, rect, str, color, multiline ?
            (DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX) :
            (DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX));
        return;
    }

    if (!hasInit) {
        Init(IDR_TTF_DEFAULT, L"TTF");
        hasInit = true;
    }
    if (scale == 0)
        scale = stbtt_ScaleForPixelHeight(&info, (float)fontSize);

    int codePoint = '?';
    int xOffset = rect.x, yOffset = rect.y;
    int areaWidth = rect.width;
    const auto len = strlen(str);
    size_t i = 0;
    while (i < len) {
        if ((str[i] & 0x80) == 0) {
            codePoint = str[i];
            i++;
        }
        else if ((str[i] & 0xe0) == 0xc0) { // 110x'xxxx 10xx'xxxx
            codePoint = ((str[i] & 0x1f) << 6) | (str[i + 1] & 0x3f);
            i += 2;
        }
        else if ((str[i] & 0xf0) == 0xe0) { // 1110'xxxx 10xx'xxxx 10xx'xxxx
            codePoint = ((str[i] & 0x0f) << 12) | ((str[i + 1] & 0x3f) << 6) | (str[i + 2] & 0x3f);
            i += 3;
        }
        else if ((str[i] & 0xf8) == 0xf0) { // 1111'0xxx 10xx'xxxx 10xx'xxxx 10xx'xxxx
            codePoint = ((str[i] & 0x07) << 18) | ((str[i + 1] & 0x3f) << 12) | ((str[i + 2] & 0x3f) << 6) | (str[i + 3] & 0x3f);
            i += 4;
        }
        else {
            codePoint = '?';
            i++;
        }

        if (codePoint == '\n' || (xOffset + fontSize) > (rect.x+rect.width)) {
            yOffset += int(fontSize * (1 + lineGapPercent));
            if (yOffset + fontSize > (rect.y+rect.height)) {
                rect.x += rect.width;
                yOffset = rect.y;

                if (rect.x >= img.cols) {
                    return;
                }
            }
            xOffset = rect.x;
            if (codePoint == '\n')
                continue;
        }

        xOffset += putWord(img, xOffset, yOffset, codePoint, color,
            isAdaptiveFG, enhanceGlyphCoverage);
    }
}

void TextDrawer::putWrappedLeft(cv::Mat& img, cv::Rect rect, const char* str, intUnion color,
    bool isAdaptiveFG, bool enhanceGlyphCoverage) {
    if (TextRenderingPolicy::usesNativeClearType(isAdaptiveFG, enhanceGlyphCoverage)) {
        drawNativeText(img, rect, str, color,
            DT_LEFT | DT_TOP | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX);
        return;
    }
    putAlignLeft(img, rect, str, color, isAdaptiveFG, enhanceGlyphCoverage);
}

void TextDrawer::drawNativeText(cv::Mat& img, cv::Rect rect, const char* str,
    intUnion color, UINT format) {
    const cv::Rect requestedRect = rect;
    const cv::Rect clippedRect = rect & cv::Rect{ 0, 0, img.cols, img.rows };
    if (requestedRect.empty() || clippedRect.empty() || !str || !*str)
        return;

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = requestedRect.width;
    bitmapInfo.bmiHeader.biHeight = -requestedRect.height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    void* bitmapBits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &bitmapInfo, DIB_RGB_COLORS,
        &bitmapBits, nullptr, 0);
    HDC memoryDc = CreateCompatibleDC(nullptr);
    if (!bitmap || !memoryDc || !bitmapBits) {
        if (memoryDc)
            DeleteDC(memoryDc);
        if (bitmap)
            DeleteObject(bitmap);
        return;
    }

    HGDIOBJ oldBitmap = SelectObject(memoryDc, bitmap);
    const std::size_t rowBytes = static_cast<std::size_t>(requestedRect.width) * 4;
    const int bitmapX = clippedRect.x - requestedRect.x;
    const int bitmapY = clippedRect.y - requestedRect.y;
    for (int row = 0; row < clippedRect.height; ++row) {
        memcpy(static_cast<uint8_t*>(bitmapBits) + (bitmapY + row) * rowBytes + bitmapX * 4,
            img.ptr(clippedRect.y + row) + clippedRect.x * 4,
            static_cast<std::size_t>(clippedRect.width) * 4);
    }

    HFONT font = CreateFontW(-fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(memoryDc, font);
    SetBkMode(memoryDc, TRANSPARENT);
    SetTextColor(memoryDc, RGB(color[2], color[1], color[0]));
    const std::wstring text = jarkUtils::utf8ToWstring(str);
    RECT nativeRect{ 0, 0, requestedRect.width, requestedRect.height };
    DrawTextW(memoryDc, text.c_str(), static_cast<int>(text.size()), &nativeRect, format);

    for (int row = 0; row < clippedRect.height; ++row) {
        auto* source = reinterpret_cast<uint32_t*>(
            static_cast<uint8_t*>(bitmapBits) + (bitmapY + row) * rowBytes) + bitmapX;
        auto* destination = reinterpret_cast<uint32_t*>(img.ptr(clippedRect.y + row)) + clippedRect.x;
        for (int column = 0; column < clippedRect.width; ++column)
            destination[column] = source[column] | 0xFF000000u;
    }

    SelectObject(memoryDc, oldFont);
    SelectObject(memoryDc, oldBitmap);
    DeleteObject(font);
    DeleteObject(bitmap);
    DeleteDC(memoryDc);
}

void TextDrawer::Init(unsigned int idi, const wchar_t* type) {
    rc = jarkUtils::GetResource(idi, type);

    if (!stbtt_InitFont(&info, rc.ptr, 0)) {
        JARK_LOG("stbtt_InitFont failed");
        if (idi != IDR_TTF_DEFAULT) {
            JARK_LOG("Reset to IDR_TTF_DEFAULT");
            Init(IDR_TTF_DEFAULT, L"TTF");
        }
        return;
    }

    auto newBufferSize = 2ULL * fontSize * fontSize;
    wordBuff.resize(newBufferSize);
    memset(wordBuff.data(), 0, newBufferSize);
    asciiCache.clear();
    asciiCache.resize(256);
}

int TextDrawer::putWord(cv::Mat& img, int x, int y, const int codePoint, intUnion color,
    bool isAdaptiveFG, bool enhanceGlyphCoverage) {
    int c_x0, c_y0, c_x1, c_y1;
    stbtt_GetCodepointBitmapBox(&info, codePoint, scale, scale, &c_x0, &c_y0, &c_x1, &c_y1);

    int wordWidth = c_x1 - c_x0;
    int wordHigh = c_y1 - c_y0;

    uint8_t* wordBuffPtr = nullptr;
    if (codePoint < 256) {
        if (asciiCache[codePoint].empty()) {
            asciiCache[codePoint].resize(wordBuff.size());
            stbtt_MakeCodepointBitmap(&info, asciiCache[codePoint].data(), wordWidth, wordHigh, fontSize, scale, scale, codePoint);
        }
        wordBuffPtr = asciiCache[codePoint].data();
    }
    else {
        stbtt_MakeCodepointBitmap(&info, wordBuff.data(), wordWidth, wordHigh, fontSize, scale, scale, codePoint);
        wordBuffPtr = wordBuff.data();
    }

    y += fontSize + c_y0;
    x += c_x0;

    for (int yy = 0; yy < wordHigh; yy++) {
        if (y + yy >= img.rows)
            break;

        auto ptr = (intUnion*)(img.ptr() + img.step1() * (y + yy));

        for (int xx = 0; xx < wordWidth; xx++) {
            if (x + xx >= img.cols)
                break;

            auto& orgColor = ptr[x + xx];

            if (isAdaptiveFG) {
                int gray = (306 * orgColor[0] + 601 * orgColor[1] + 117 * orgColor[2]) / 1024;
                color = gray < 128 ? deepTheme.FG : lightTheme.FG;
            }

            const uint8_t rawCoverage = wordBuffPtr[yy * fontSize + xx];
            const uint8_t coverage = enhanceGlyphCoverage ?
                TextRenderingPolicy::enhanceCoverage(rawCoverage) : rawCoverage;
            int alpha = coverage * color[3] / 255;
            if (alpha)
                orgColor = {
                    (uint8_t)((orgColor[0] * (255 - alpha) + color[0] * alpha + 255) >> 8),
                    (uint8_t)((orgColor[1] * (255 - alpha) + color[1] * alpha + 255) >> 8),
                    (uint8_t)((orgColor[2] * (255 - alpha) + color[2] * alpha + 255) >> 8),
                    255 };
        }
    }

    const int size = int(fontSize * (1 + lineGapPercent));
    return codePoint < 256 ? (size / 2) : size;
}
