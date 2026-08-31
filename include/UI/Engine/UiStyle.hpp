#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace kc {

enum class RenderMode : int {
    Solid          = -1,
    Font           =  0,
    GradientLinear = -2,
    GradientRadial = -3,
    RoundedRect    = -4,
    Texture        = -5,
};

struct GpuStyle {
    glm::vec4 color1;
    glm::vec4 color2;
    glm::vec4 params;
};

static_assert(sizeof(GpuStyle) == 48, "GpuStyle must be 48 bytes (3 x vec4)");
constexpr uint32_t GPU_STYLE_STRIDE = 48;

struct UiStyle {
    RenderMode mode = RenderMode::Solid;
    glm::vec4 color1{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 color2{0.0f, 0.0f, 0.0f, 0.0f};
    float cornerRadius = 0.0f;
    float borderWidth = 0.0f;
    float coverage = 1.0f;
    float textureLayer = -1.0f;
    glm::vec2 gradientDir{1.0f, 0.0f};
    glm::vec2 gradientCenter{0.5f, 0.5f};

    GpuStyle toGpu() const {
        GpuStyle gpu{};
        gpu.color1 = color1;
        gpu.color2 = color2;
        gpu.params.x = static_cast<float>(static_cast<int>(mode));
        gpu.params.z = borderWidth;
        gpu.params.w = coverage;
        if (mode == RenderMode::Texture) {
            gpu.params.y = textureLayer;
        } else {
            gpu.params.y = cornerRadius;
        }
        return gpu;
    }
};

} // namespace kc
