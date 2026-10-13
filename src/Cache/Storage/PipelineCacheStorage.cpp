#include "Cache/Storage/PipelineCacheStorage.hpp"

#include "Cache/Constants/PipelineCacheConstants.hpp"
#include "Threads/IO.hpp"
#include "Util/LogUtils.hpp"

namespace kc {
    namespace {
        GenericCacheTemplate& cache() {
            return IO::Get().getBuiltinTemplates().getCacheTemplate();
        }
    }

    bool PipelineCacheStorage::load() {
        if (!cache().readHeader(PIPELINE_CACHE_FILENAME, header_)) {
            LogUtils::info(ThreadName::Engine, "[PipelineCache] No cache found, starting fresh.");
            return false;
        }

        if (!validator_.validate(header_)) {
            return false;
        }

        auto payload = cache().readPayload(PIPELINE_CACHE_FILENAME, sizeof(PipelineCacheHeader));
        if (!payload || payload->empty()) {
            LogUtils::warn(ThreadName::Engine, "[PipelineCache] Cache payload is missing or empty.");
            return false;
        }

        payload_ = std::move(*payload);
        return true;
    }

    bool PipelineCacheStorage::validateDevice(const PipelineCacheValidationInfo& info) {
        return validator_.validateVulkan(info, header_);
    }

    void PipelineCacheStorage::save(const PipelineCacheHeader& header, const std::vector<char>& blob) {
        cache().write(PIPELINE_CACHE_FILENAME, header, blob);
    }
}
