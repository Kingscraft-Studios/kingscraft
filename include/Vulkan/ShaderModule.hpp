#pragma once

#include "Device.hpp"
#include <vector>

namespace kc {

    class ShaderModule {
    public:
        ShaderModule(Device& device, const std::vector<char>& code);
        ~ShaderModule();

        ShaderModule(const ShaderModule&) = delete;
        ShaderModule& operator=(const ShaderModule&) = delete;

        ShaderModule(ShaderModule&& other) noexcept;
        ShaderModule& operator=(ShaderModule&& other) noexcept;

        VkShaderModule getHandle() const { return module_; }
        void reset();

    private:
        Device& device_;
        VkShaderModule module_ = VK_NULL_HANDLE;
    };

} // namespace kc