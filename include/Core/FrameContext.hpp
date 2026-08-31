#pragma once

#include <vulkan/vulkan.h>

namespace kc {

class PostProcessing;

struct FrameContext {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkExtent2D extent{};

    double dt = 0.0;
    uint32_t frameIndex = 0;
    uint32_t imageIndex = 0;

    VkQueryPool gpuQueryPool = VK_NULL_HANDLE;
    PostProcessing* postProcessing = nullptr;

    double cpuFrameTimeMs = 0.0;
    double cpuTickMs = 0.0;
    double cpuSubmitMs = 0.0;
};

} // namespace kc
