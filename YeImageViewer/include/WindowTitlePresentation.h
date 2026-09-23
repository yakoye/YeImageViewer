#pragma once

#include <string>
#include <string_view>

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

// 「1.6 MiB」压成「1.6MB」：去掉空格，单位按 Windows 资源管理器的习惯写 KB/MB/GB。
// 数值本身仍是 1024 进制，和资源管理器显示的一致，只是不写成 KiB。
inline std::wstring compactSize(std::wstring_view text) {
    std::wstring result;
    result.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        const wchar_t character = text[index];
        if (character == L' ')
            continue;
        if (character == L'i' && index > 0 &&
            (text[index - 1] == L'K' || text[index - 1] == L'M' ||
                text[index - 1] == L'G' || text[index - 1] == L'T'))
            continue;   // KiB -> KB
        result.push_back(character);
    }
    return result;
}

// 标题格式：[08/22] 名称.png 671x477(108.0KB) 110%
//
// 序号补零而不是补空格：标题栏是比例字体，空格比数字窄，补空格照样会随序号位数
// 变化左右跳；补零宽度才真的固定。
//
// 状态（动图暂停）和旋转角度排在最后：这两项时有时无，放在中间会把后面的字段
// 整体推走，跳动比序号还明显。
inline std::wstring build(const Model& model) {
    std::wstring result;

    if (model.current > 0 && model.total > 0) {
        const std::wstring total = std::to_wstring(model.total);
        std::wstring current = std::to_wstring(model.current);
        if (current.size() < total.size())
            current.insert(0, total.size() - current.size(), L'0');
        result += L"[" + current + L"/" + total + L"] ";
    }

    if (!model.fileName.empty())
        result += model.fileName;

    if (model.pixelWidth > 0 && model.pixelHeight > 0) {
        if (!result.empty() && result.back() != L' ')
            result += L' ';
        result += std::to_wstring(model.pixelWidth) + L"x" + std::to_wstring(model.pixelHeight);
        if (!model.fileSize.empty())
            result += L"(" + compactSize(model.fileSize) + L")";
    }
    else if (!model.fileSize.empty()) {
        if (!result.empty() && result.back() != L' ')
            result += L' ';
        result += L"(" + compactSize(model.fileSize) + L")";
    }

    if (!result.empty() && result.back() != L' ')
        result += L' ';
    result += std::to_wstring(model.zoomPercent) + L"%";

    if (!model.state.empty())
        result += L" " + model.state;
    if (!model.rotation.empty())
        result += L" " + model.rotation;

    return result;
}

}
