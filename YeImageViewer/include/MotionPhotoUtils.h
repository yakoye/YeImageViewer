#pragma once

#include <charconv>
#include <cstddef>
#include <string_view>

namespace MotionPhotoUtils {

inline size_t getVideoSize(std::string_view exifStr) noexcept {
    constexpr std::string_view microVideoOffsetKey = "Xmp.GCamera.MicroVideoOffset: ";
    constexpr std::string_view motionPhotoSemantic = "Item:Semantic: MotionPhoto";
    constexpr std::string_view motionPhotoLengthKey = "Item:Length: ";

    size_t valueStart = 0;
    size_t pos = exifStr.find(microVideoOffsetKey);
    if (pos != std::string_view::npos) {
        valueStart = pos + microVideoOffsetKey.length();
    }
    else {
        pos = exifStr.find(motionPhotoSemantic);
        if (pos == std::string_view::npos) {
            return 0;
        }

        pos = exifStr.find(motionPhotoLengthKey, pos);
        if (pos == std::string_view::npos) {
            return 0;
        }
        valueStart = pos + motionPhotoLengthKey.length();
    }

    size_t valueEnd = valueStart;
    while (valueEnd < exifStr.length() && exifStr[valueEnd] >= '0' && exifStr[valueEnd] <= '9') {
        ++valueEnd;
    }
    if (valueEnd == valueStart) {
        return 0;
    }

    size_t value = 0;
    const auto [ptr, error] = std::from_chars(exifStr.data() + valueStart, exifStr.data() + valueEnd, value);
    if (error != std::errc{} || ptr != exifStr.data() + valueEnd) {
        return 0;
    }
    return value;
}

}
