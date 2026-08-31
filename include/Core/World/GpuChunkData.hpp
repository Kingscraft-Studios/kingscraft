#pragma once
#include <memory>

#include "Vulkan/Buffer.hpp"
#include "Vulkan/Fence.hpp"

namespace lve {
    struct GpuChunkData {
        std::unique_ptr<Buffer> vertexBuffer;
        std::unique_ptr<Buffer> indexBuffer;
        uint32_t indexCount = 0;

        std::unique_ptr<Fence> uploadFence;
        VkCommandBuffer uploadCmd = VK_NULL_HANDLE;

        void cleanup(Device& device);
    };
}
