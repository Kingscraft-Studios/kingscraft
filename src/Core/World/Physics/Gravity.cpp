#include "Core/World/Physics/Gravity.hpp"

#include "Core/Attributes.hpp"

#include <algorithm>

namespace lve {

    float Gravity::apply(float velocityY, float dt) {
        float v = velocityY + Attributes::GRAVITY * dt;
        return std::max(v, -Attributes::TERMINAL_FALL_SPEED);
    }

} // namespace lve
