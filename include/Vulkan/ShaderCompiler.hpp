#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Cache/Constants/ShaderCacheConstants.hpp"

namespace kc {
    struct ShaderCompilerOptions {
        // TODO: FIX
        uint32_t vulkanVersion = 0;      // 0 => engine default (Vulkan 1.2)
        bool optimize = true;
        std::string entryPoint = "main";
    };

    // Multipurpose GLSL -> SPIR-V compiler (thin, reusable wrapper over shaderc).
    class ShaderCompiler {
    public:
        static std::optional<ShaderStage> stageFromExtension(const std::string& path);

        // `name` is only used for diagnostics. Returns SPIR-V bytes, or nullopt on failure.
        std::optional<std::vector<char>> compile(const std::string& source,
                                                 ShaderStage stage,
                                                 const std::string& name,
                                                 const ShaderCompilerOptions& options = {}) const;
    };
}
