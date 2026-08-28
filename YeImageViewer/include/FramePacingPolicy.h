#pragma once

namespace FramePacingPolicy {

// Present once per display refresh. This prevents unpaced CPU frames from
// arriving in uneven bursts during zoom and pan animations.
inline constexpr unsigned PRESENT_SYNC_INTERVAL = 1;

constexpr bool usesDisplaySynchronizedPresent() {
    return PRESENT_SYNC_INTERVAL == 1;
}

}
