#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace WindowTitlePresentation {

struct Model {
    std::wstring state;
    int current = 0;
    int total = 0;
    int zoomPercent = 100;
    int pixelWidth = 0;
    int pixelHeight = 0;
    std::wstring fileSize;
    std::wstring fileName;
    std::wstring rotation;
};

inline std::wstring build(const Model& model) {
    std::vector<std::wstring> parts;
    if (!model.state.empty())
        parts.push_back(model.state);
    if (model.current > 0 && model.total > 0)
        parts.push_back(L"[" + std::to_wstring(model.current) + L"/" +
            std::to_wstring(model.total) + L"]");
    parts.push_back(std::to_wstring(model.zoomPercent) + L"%");
    if (model.pixelWidth > 0 && model.pixelHeight > 0) {
        parts.push_back(std::to_wstring(model.pixelWidth) + L" × " +
            std::to_wstring(model.pixelHeight) + L" px");
    }
    if (!model.fileSize.empty())
        parts.push_back(model.fileSize);
    if (!model.fileName.empty())
        parts.push_back(model.fileName);
    if (!model.rotation.empty())
        parts.push_back(model.rotation);

    std::wstring result;
    for (const auto& part : parts) {
        if (!result.empty())
            result += L" | ";
        result += part;
    }
    return result;
}

}
