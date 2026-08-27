#pragma once

namespace EscapeBehavior {

enum class Action {
    ExitFullScreen,
    RestoreWindow,
    CloseImage,
    Ignore,
};

constexpr Action resolve(bool isFullScreen, bool isMaximized, bool escapeClosesImage) {
    if (isFullScreen) return Action::ExitFullScreen;
    if (isMaximized) return Action::RestoreWindow;
    return escapeClosesImage ? Action::CloseImage : Action::Ignore;
}

}
