#pragma once
#include <algorithm>
#include <cmath>

namespace KeyboardPanMotion {
struct Vector { double x = 0, y = 0; };
class Motion {
    Vector velocity{}, residual{};
public:
    void reset() { velocity = {}; residual = {}; }
    Vector move(Vector direction, double seconds, double speed) {
        if (!std::isfinite(seconds) || !std::isfinite(speed) || seconds <= 0 || speed <= 0) return {};
        seconds = (std::min)(seconds, .05);
        const double norm = std::hypot(direction.x, direction.y);
        if (norm == 0) { reset(); return {}; }
        if (direction.x == 0) { velocity.x = 0; residual.x = 0; }
        if (direction.y == 0) { velocity.y = 0; residual.y = 0; }
        if (norm > 1) { direction.x /= norm; direction.y /= norm; }
        constexpr double tau = .055;
        const double decay = std::exp(-seconds / tau);
        const Vector target{direction.x * speed, direction.y * speed};
        Vector delta{target.x * seconds + (velocity.x - target.x) * tau * (1 - decay),
            target.y * seconds + (velocity.y - target.y) * tau * (1 - decay)};
        velocity = {target.x + (velocity.x - target.x) * decay, target.y + (velocity.y - target.y) * decay};
        residual.x += delta.x; residual.y += delta.y;
        delta = {std::trunc(residual.x), std::trunc(residual.y)};
        residual.x -= delta.x; residual.y -= delta.y;
        return delta;
    }
};
}
