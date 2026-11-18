#pragma once

#include "Device.hpp"

namespace kc {

    class PipelineLayout {
    public:
        PipelineLayout(Device& device, const VkPipelineLayoutCreateInfo& info);
        ~PipelineLayout();

        PipelineLayout(const PipelineLayout&) = delete;
        PipelineLayout& operator=(const PipelineLayout&) = delete;

        PipelineLayout(PipelineLayout&& other) noexcept;
        PipelineLayout& operator=(PipelineLayout&& other) noexcept;

        VkPipelineLayout getHandle() const { return layout_; }
        void reset();

    private:
        Device& device_;
        VkPipelineLayout layout_ = VK_NULL_HANDLE;
    };

} // namespace kc