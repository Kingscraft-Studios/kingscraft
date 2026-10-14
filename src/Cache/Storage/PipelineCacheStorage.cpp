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
        deviceValidated_ = false;

        return true;
    }

    void PipelineCacheStorage::setDeviceInfo(const PipelineCacheValidationInfo& info) {
        deviceInfo_ = info;
        deviceValidated_ = false;
    }

    const std::vector<char>& PipelineCacheStorage::getPipelineCache() {
        if (!deviceValidated_ && !payload_.empty()) {
            deviceValidated_ = true;
            if (!validator_.validateVulkan(deviceInfo_, header_)) {
                payload_.clear();
            }
        }
        return payload_;
    }

    void PipelineCacheStorage::save(const PipelineCacheHeader& header, const std::vector<char>& blob) {
        cache().write(PIPELINE_CACHE_FILENAME, header, blob);
    }
}
