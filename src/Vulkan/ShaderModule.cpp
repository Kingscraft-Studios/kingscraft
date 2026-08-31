#include "Vulkan/ShaderModule.hpp"
#include <stdexcept>

namespace kc {

    ShaderModule::ShaderModule(Device& device, const std::vector<char>& code)
        : device_(device) {
        VkShaderModuleCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = code.size();
        info.pCode = reinterpret_cast<const uint32_t*>(code.data());
        if (vkCreateShaderModule(device_.device(), &info, nullptr, &module_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create shader module!");
        }
    }

    ShaderModule::~ShaderModule() {
        if (module_ != VK_NULL_HANDLE)
            vkDestroyShaderModule(device_.device(), module_, nullptr);
    }

    ShaderModule::ShaderModule(ShaderModule&& other) noexcept
        : device_(other.device_) {
        module_ = other.module_;
        other.module_ = VK_NULL_HANDLE;
    }

    ShaderModule& ShaderModule::operator=(ShaderModule&& other) noexcept {
        if (this != &other) {
            if (module_ != VK_NULL_HANDLE)
                vkDestroyShaderModule(device_.device(), module_, nullptr);
            module_ = other.module_;
            other.module_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    void ShaderModule::reset() {
        if (module_ != VK_NULL_HANDLE)
            vkDestroyShaderModule(device_.device(), module_, nullptr);
        module_ = VK_NULL_HANDLE;
    }

} // namespace kc