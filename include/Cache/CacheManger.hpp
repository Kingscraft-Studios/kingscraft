#pragma once
#include "Storage/PipelineCacheStorage.hpp"

namespace kc {
    class CacheManager {
    public:
        PipelineCacheStorage& getPipelineCache() { return pipelineCacheStorage; }
    private:
        PipelineCacheStorage pipelineCacheStorage;
    };
}
