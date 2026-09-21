#pragma once

namespace EscapeBehavior {

enum class Action {
    ExitPresentation,
    ExitFullScreen,
    RestoreWindow,
    Ignore,
};

// 只有 Esc 没被任何快捷键占用时才走到这里。关闭图片曾经是这里的一个分支，
// 现在是快捷键 Action::CloseImage（默认就绑在 Esc 上），按键先由快捷键分派，
// 分派不掉才由这里决定是退出沉浸预览、退出全屏还是还原窗口。
constexpr Action resolve(bool isPresentation, bool isFullScreen, bool isMaximized) {
    if (isPresentation) return Action::ExitPresentation;
    if (isFullScreen) return Action::ExitFullScreen;
    if (isMaximized) return Action::RestoreWindow;
    return Action::Ignore;
}

}
