#pragma once

#include "OverlayLayout.h"

namespace ToolbarCommand {

enum class Command {
    None,
    PreviousImage,
    PlayPause,
    NextImage,
    RotateLeft,
    RotateRight,
    FlipHorizontal,
    FlipVertical,
    ZoomFit,
    ZoomActual,
    Fullscreen,
    Settings,
    ZoomOut,
    EditZoom,
    ZoomIn,
};

constexpr Command resolve(OverlayLayout::Hit hit) {
    switch (hit) {
    case OverlayLayout::Hit::ToolbarPreviousImage: return Command::PreviousImage;
    case OverlayLayout::Hit::ToolbarPlayPause: return Command::PlayPause;
    case OverlayLayout::Hit::ToolbarNextImage: return Command::NextImage;
    case OverlayLayout::Hit::RotateLeft: return Command::RotateLeft;
    case OverlayLayout::Hit::RotateRight: return Command::RotateRight;
    case OverlayLayout::Hit::FlipHorizontal: return Command::FlipHorizontal;
    case OverlayLayout::Hit::FlipVertical: return Command::FlipVertical;
    case OverlayLayout::Hit::ZoomFit: return Command::ZoomFit;
    case OverlayLayout::Hit::ZoomActual: return Command::ZoomActual;
    case OverlayLayout::Hit::Fullscreen: return Command::Fullscreen;
    case OverlayLayout::Hit::Settings: return Command::Settings;
    case OverlayLayout::Hit::ZoomOut: return Command::ZoomOut;
    case OverlayLayout::Hit::ZoomText: return Command::EditZoom;
    case OverlayLayout::Hit::ZoomIn: return Command::ZoomIn;
    default: return Command::None;
    }
}

} // namespace ToolbarCommand
