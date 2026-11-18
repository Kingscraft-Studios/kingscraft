#pragma once
#include <memory>

#include "Vulkan/Buffer.hpp"
#include "Vulkan/Fence.hpp"

namespace kc {
    struct GpuChunkData {
        std::unique_ptr<Buffer> vertexBuffer;
        std::unique_ptr<Buffer> indexBuffer;
        uint32_t indexCount = 0;

        // Transparent stream (water/glass): drawn after the opaque pass with
        // blending enabled and depth writes off.
        std::unique_ptr<Buffer> transparentVertexBuffer;
        std::unique_ptr<Buffer> transparentIndexBuffer;
        uint32_t transparentIndexCount = 0;

        std::unique_ptr<Fence> uploadFence;
        VkCommandBuffer uploadCmd = VK_NULL_HANDLE;

        void cleanup(Device& device);
    };
}
