#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

// 所有配置塞进 YeImageViewer.db 一个文件：前 4096 字节是固定大小的设置结构体，
// 之后是 UTF-8 文本区，外部编辑器、复制/移动目标、图片旋转记录都写在文本区里。
//
// 这样绿色版落地就是三个文件——本体、缩略图 DLL、一个配置，
// 不再多出 YeImageViewer.editors.ini 和 YeImageViewer.rotations.db。
//
// 文本区只追加在结构体后面，设置的读写仍然只碰前 4096 字节，所以新旧版本的
// 配置文件互相都读得了：老版本读新文件会忽略尾巴，新版本读老文件只是尾巴为空。
//
// 文本区是 `键=值` 的行，每一块各自认自己的键前缀，保存时把不认识的行原样写回去
// （见各模块的 foreignLines）。三块数据共用一个文件，谁都不能把别人的行抹掉。
namespace ConfigFile {

// 必须与 SettingParameter 的大小一致。设置结构体是固定 4096 字节的，
// 这个数字变了会把已有配置文件里的文本区错位读成乱码。
inline constexpr std::streamoff HEAD_SIZE = 4096;

namespace detail {

inline std::vector<std::string> splitLines(std::istream& stream) {
    std::vector<std::string> lines;
    std::string line;
    bool firstLine = true;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (firstLine && line.starts_with("\xEF\xBB\xBF"))
            line.erase(0, 3);
        firstLine = false;
        lines.push_back(line);
    }
    return lines;
}

}  // namespace detail

// 读文本区。文件不存在、或还没长到 4096 字节（只有设置、没写过文本）时返回空。
inline std::vector<std::string> readLines(const std::wstring& filePath) {
    if (filePath.empty())
        return {};
    std::ifstream file(std::filesystem::path(filePath), std::ios::binary);
    if (!file)
        return {};
    file.seekg(0, std::ios::end);
    if (!file || file.tellg() <= HEAD_SIZE)
        return {};
    file.seekg(HEAD_SIZE, std::ios::beg);
    if (!file)
        return {};
    return detail::splitLines(file);
}

// 整份当纯文本读。只给迁移旧的 .editors.ini 用——那种文件没有 4096 字节的头。
inline std::vector<std::string> readPlainLines(const std::wstring& filePath) {
    if (filePath.empty())
        return {};
    std::ifstream file(std::filesystem::path(filePath), std::ios::binary);
    if (!file)
        return {};
    return detail::splitLines(file);
}

// 写文本区，原样保留前 4096 字节。
//
// 文件不存在或不足 4096 字节时用零补齐：补出来的头通不过设置文件的魔数校验，
// 这一轮设置按默认值走，等程序退出时再把真正的设置写回前 4096 字节。
// 只有「先配置编辑器、还没退出过程序」这一种情况会碰到，代价可以接受——
// 反之如果这里拒绝写，用户的配置就直接丢了。
inline bool writeLines(const std::wstring& filePath,
    const std::vector<std::string>& lines) {
    if (filePath.empty())
        return false;

    std::vector<char> head(static_cast<std::size_t>(HEAD_SIZE), '\0');
    {
        std::ifstream existing(std::filesystem::path(filePath), std::ios::binary);
        if (existing)
            existing.read(head.data(), HEAD_SIZE);
    }

    std::ofstream file(std::filesystem::path(filePath),
        std::ios::binary | std::ios::trunc);
    if (!file)
        return false;
    file.write(head.data(), HEAD_SIZE);
    for (const auto& line : lines)
        file << line << "\r\n";
    file.flush();
    return file.good();
}

// 从一行里切出键。不是 `键=值` 的行返回空。
inline std::string_view keyOf(std::string_view line) {
    const auto equals = line.find('=');
    if (equals == std::string_view::npos)
        return {};
    return line.substr(0, equals);
}

inline std::string_view valueOf(std::string_view line) {
    const auto equals = line.find('=');
    if (equals == std::string_view::npos)
        return {};
    return line.substr(equals + 1);
}

// 把不属于自己的行挑出来。三块数据共用一个文本区，各自保存时都要把别人的行带上。
inline std::vector<std::string> foreignLines(const std::vector<std::string>& lines,
    bool (*isMine)(std::string_view)) {
    std::vector<std::string> kept;
    for (const auto& line : lines) {
        if (line.empty() || line.front() == '[')
            continue;
        const auto key = keyOf(line);
        if (key.empty() || isMine(key))
            continue;
        kept.push_back(line);
    }
    return kept;
}

}  // namespace ConfigFile
