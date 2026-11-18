#pragma once
#include "Storage/PipelineCacheStorage.hpp"
#include "Storage/ShaderCacheStorage.hpp"

namespace kc {
    class CacheManager {
    public:
        PipelineCacheStorage& getPipelineCache() { return pipelineCacheStorage; }
        ShaderCacheStorage& getShaderCache() { return shaderCacheStorage; }
    private:
        PipelineCacheStorage pipelineCacheStorage;
        ShaderCacheStorage shaderCacheStorage;
    };
}
