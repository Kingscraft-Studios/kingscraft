#pragma once
#include <string>

#include "vulkan/vulkan_core.h"

namespace lve {
    struct WindowCreateInfo {
        int width;
        int height;
        std::string name;
    };

    struct WindowExtent {
        uint32_t width;
        uint32_t height;

        VkExtent2D toVKExtent() const {
            return {width, height};
        }
    };
}
