#pragma once

#include <cstdint>
#include <cstring>
#include <span>

// psd_sdk 自带的 NativeFile 是按路径从磁盘读的，而两边调用方手上都已经有整个文件的
// 字节了（主程序的 myLoader 读过一遍，缩略图组件拿到的本来就是一个流）。再让它自己
// 读一遍就是白读一次整文件——PSD 动辄几百 MB。
//
// 这个类把 psd::File 接到一块内存上，只读，写一律拒绝。
//
// 注意：包含这个头文件就等于包含 psdsdk.h，而 psd_sdk 的 PsdPlatform.h 会
// #define NOMSG / NOUSER / NOGDI 一整串把 windows.h 砍瘦。只要它排在 COM 头之前，
// 后面的 oleidl.h / ocidl.h 就会因为缺 MSG 编译失败。所以要么像
// ThumbPsd.cpp 那样单独一个编译单元，要么确保 windows.h 已经先展开过了。
namespace PsdSupport {

class MemoryFile final : public psd::File {
public:
    MemoryFile(psd::Allocator* allocator, std::span<const uint8_t> data) noexcept
        : psd::File(allocator), m_data(data.data()), m_size(data.size()) {}

private:
    bool DoOpenRead(const wchar_t*) override { return m_data != nullptr && m_size > 0; }
    bool DoOpenWrite(const wchar_t*) override { return false; }
    bool DoClose(void) override { return true; }

    ReadOperation DoRead(void* buffer, uint32_t count, uint64_t position) override {
        if (!buffer || position > m_size)
            return nullptr;

        const size_t available = m_size - static_cast<size_t>(position);
        const size_t copied = count < available ? count : available;
        std::memcpy(buffer, m_data + position, copied);
        if (copied < count) {
            // 读过界的部分填零：截断的文件不至于读到未初始化内存
            std::memset(static_cast<uint8_t*>(buffer) + copied, 0, count - copied);
        }
        // 同步读已经完成，返回一个非空句柄表示「这次操作有效」
        return reinterpret_cast<ReadOperation>(static_cast<uintptr_t>(1));
    }

    bool DoWaitForRead(ReadOperation& operation) override {
        const bool ok = operation != nullptr;
        operation = nullptr;
        return ok;
    }

    WriteOperation DoWrite(const void*, uint32_t, uint64_t) override { return nullptr; }
    bool DoWaitForWrite(WriteOperation&) override { return false; }
    uint64_t DoGetSize(void) const override { return m_size; }

    const uint8_t* m_data = nullptr;
    size_t m_size = 0;
};

} // namespace PsdSupport
