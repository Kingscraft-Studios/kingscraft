#include "UI/Engine/UiBatchQueue.hpp"

#include <cstring>

namespace kc {

    UiBatchQueue::UiBatchQueue(Device& device) : device_(device) {
        growBuffers(vertexCapacity_, indexCapacity_);
    }

    UiBatchQueue::~UiBatchQueue() = default;

    void UiBatchQueue::begin() {
        vertices_.clear();
        indices_.clear();
    }

    void UiBatchQueue::addQuad(const UiVertex verts[4], const uint32_t indices[6]) {
        size_t vertOffset = vertices_.size();
        vertices_.resize(vertOffset + 4);
        std::memcpy(vertices_.data() + vertOffset, verts, 4 * sizeof(UiVertex));

        size_t idxOffset = indices_.size();
        indices_.resize(idxOffset + 6);
        for (int i = 0; i < 6; i++) {
            indices_[idxOffset + i] = indices[i] + static_cast<uint32_t>(vertOffset);
        }
    }

    void UiBatchQueue::flush(VkCommandBuffer cmd) {
        if (vertices_.empty() || indices_.empty()) return;

        if (vertices_.size() > vertexCapacity_ || indices_.size() > indexCapacity_) {
            growBuffers(vertices_.size(), indices_.size());
        }

        vertexBuffer_->write(vertices_.data(), 0, vertices_.size() * sizeof(UiVertex));
        indexBuffer_->write(indices_.data(), 0, indices_.size() * sizeof(uint32_t));

        VkBuffer vertexBuffer = vertexBuffer_->getHandle();
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, offsets);
        vkCmdBindIndexBuffer(cmd, indexBuffer_->getHandle(), 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexed(cmd, static_cast<uint32_t>(indices_.size()), 1, 0, 0, 0);
    }

    void UiBatchQueue::growBuffers(size_t minVerts, size_t minIndices) {
        while (vertexCapacity_ < minVerts) vertexCapacity_ *= 2;
        while (indexCapacity_ < minIndices) indexCapacity_ *= 2;

        VkDeviceSize vertSize = vertexCapacity_ * sizeof(UiVertex);
        VkDeviceSize idxSize = indexCapacity_ * sizeof(uint32_t);

        vertexBuffer_ = std::make_unique<Buffer>(
            device_, vertSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        indexBuffer_ = std::make_unique<Buffer>(
            device_, idxSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }

} // namespace kc
