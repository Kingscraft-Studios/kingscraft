#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Cache/Constants/ShaderCacheConstants.hpp"
#include "Cache/Validators/ShaderCacheValidator.hpp"
#include "Vulkan/ShaderCompiler.hpp"

namespace kc {
    class ShaderCacheStorage {
    public:
        void load();

        bool has(const std::string& name, ShaderStage stage) const;
        // Throws if the Shader was not discovered/compiled.
        const std::vector<char>& get(const std::string& name, ShaderStage stage) const;

        size_t size() const { return shaders.size(); }

    private:
        bool loadCached(const std::string& key, const std::string& cacheName,
                        const std::array<uint8_t, 16>& hash, std::vector<char>& out);

        ShaderCacheValidator validator_;
        ShaderCompiler compiler_;
        std::unordered_map<std::string, std::vector<char>> shaders;
    };
}
