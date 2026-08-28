#pragma once

namespace EscapeBehavior {

enum class Action {
    ExitPresentation,
    ExitFullScreen,
    RestoreWindow,
    CloseImage,
    Ignore,
};

constexpr Action resolve(bool isPresentation, bool isFullScreen,
    bool isMaximized, bool escapeClosesImage) {
    // The explicit preference is authoritative in every viewer mode.
    if (escapeClosesImage) return Action::CloseImage;
    if (isPresentation) return Action::ExitPresentation;
    if (isFullScreen) return Action::ExitFullScreen;
    if (isMaximized) return Action::RestoreWindow;
    return Action::Ignore;
}

}
