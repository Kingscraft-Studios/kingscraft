#pragma once
#include <vector>

#include "Cache/Constants/PipelineCacheConstants.hpp"
#include "Cache/Validators/PipelineCacheValidator.hpp"

namespace kc {
    class PipelineCacheStorage {
    public:
        // Reads + validates the header, then reads the payload. Keeps both on success.
        bool load();

        void setDeviceInfo(const PipelineCacheValidationInfo& info);

        const std::vector<char>& getPipelineCache();

        // Persists the header immediately followed by the blob.
        void save(const PipelineCacheHeader& header, const std::vector<char>& blob);

    private:
        PipelineCacheValidator validator_;
        PipelineCacheHeader header_{};
        PipelineCacheValidationInfo deviceInfo_{};
        bool deviceValidated_ = false;
        std::vector<char> payload_;
    };
}
