#pragma once
#include <array>
#include <cstdint>

namespace kc {
    constexpr uint32_t SHADER_CACHE_MAGIC = 0x4B534348; // "KSCH"
    constexpr uint32_t SHADER_CACHE_VERSION = 1;        // bump to invalidate all cached SPIR-V

    constexpr const char* SHADER_SOURCE_DIR = "resources/shaders";
    constexpr const char* SHADER_CACHE_DIR  = "shaders"; // under cache/

    constexpr const char* SHADER_CACHE_FILE_EXTENSION = ".compiled.cache";

    enum class ShaderStage {
        Vertex,
        Fragment,
        Compute
    };

    constexpr const char* shaderStageSuffix(ShaderStage stage) {
        switch (stage) {
            case ShaderStage::Vertex:   return "vert";
            case ShaderStage::Fragment: return "frag";
            case ShaderStage::Compute:  return "comp";
        }
        return "spv";
    }

    struct ShaderCacheHeader {
        uint32_t magic;
        uint32_t version;
        std::array<uint8_t, 16> sourceHash;
    };
    static_assert(sizeof(ShaderCacheHeader) == 24, "ShaderCacheHeader layout must be stable");
}
