#pragma once
#include <array>
#include <cstdint>

// 只路由取色会话已声明的按键。无文件/网络/文字记录，失焦、菜单和系统组合键全部让行。
namespace PickerInput {
constexpr bool managed(uint32_t key) {
    return key == 0x10 || key == 0xA0 || key == 0xA1 || key == 'P' || key == 'C' || key == 'E' ||
        key == 0x0D || key == 0x1B || key == 0x20 || key == 0x71 || key == 0x74 ||
        (key >= 0x25 && key <= 0x28);
}
struct Route { bool consume=false, dispatch=false, repeated=false; uint32_t key=0; };
class State {
    std::array<bool,256> held{};
public:
    Route route(uint32_t key, bool down, bool active, bool menu, bool systemChord) {
        if(key >= held.size() || !managed(key)) return {};
        const bool repeated=held[key];held[key]=down;
        if(!active || menu || systemChord) return {};
        return {true,down,repeated,(key == 0xA0 || key == 0xA1) ? 0x10u : key};
    }
};
}
