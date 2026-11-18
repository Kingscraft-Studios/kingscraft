#pragma once

#include "Vulkan/Buffer.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <memory>

namespace kc {

    struct UiVertex {
        glm::vec2 pos;
        glm::vec2 uv;
        glm::vec4 color;
        uint32_t elementId = 0;
    };

    class UiBatchQueue {
    public:
        explicit UiBatchQueue(Device& device);
        ~UiBatchQueue();

        UiBatchQueue(const UiBatchQueue&) = delete;
        UiBatchQueue& operator=(const UiBatchQueue&) = delete;

        void begin();
        void addQuad(const UiVertex verts[4], const uint32_t indices[6]);
        void flush(VkCommandBuffer cmd);

    private:
        void growBuffers(size_t minVerts, size_t minIndices);

        Device& device_;
        std::unique_ptr<Buffer> vertexBuffer_;
        std::unique_ptr<Buffer> indexBuffer_;

        std::vector<UiVertex> vertices_;
        std::vector<uint32_t> indices_;

        size_t vertexCapacity_ = 1024;
        size_t indexCapacity_ = 3072;
    };

} // namespace kc
