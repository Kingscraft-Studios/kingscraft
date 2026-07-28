#pragma once
#include <memory>

#include "Vulkan/Buffer.hpp"

namespace lve {
    struct GpuChunkData {
        std::unique_ptr<Buffer> vertexBuffer;
        std::unique_ptr<Buffer> indexBuffer;
        uint32_t indexCount = 0;

        VkFence uploadFence = VK_NULL_HANDLE;
        VkCommandBuffer uploadCmd = VK_NULL_HANDLE;

        void cleanup(Device& device);
    };
}
