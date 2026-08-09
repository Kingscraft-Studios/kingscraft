#include "Core/World/Physics/Gravity.hpp"

#include "Core/Attributes.hpp"

namespace lve {

    float Gravity::apply(float velocityY, float dt) {
        return velocityY + Attributes::GRAVITY * dt;
    }

} // namespace lve
