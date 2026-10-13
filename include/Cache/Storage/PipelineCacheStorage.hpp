#pragma once
#include <vector>

#include "Cache/Constants/PipelineCacheConstants.hpp"
#include "Cache/Validators/PipelineCacheValidator.hpp"

namespace kc {
    class PipelineCacheStorage {
    public:
        // Reads + validates the header, then reads the payload. Keeps both on success.
        bool load();

        // Validates the loaded header against the actual Vulkan device.
        bool validateDevice(const PipelineCacheValidationInfo& info);

        const std::vector<char>& getPipelineCache() const { return payload_; }

        // Persists the header immediately followed by the blob.
        void save(const PipelineCacheHeader& header, const std::vector<char>& blob);

    private:
        PipelineCacheValidator validator_;
        PipelineCacheHeader header_{};
        std::vector<char> payload_;
    };
}
