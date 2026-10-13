#pragma once
#include "Cache/Constants/PipelineCacheConstants.hpp"


namespace kc {
    class PipelineCacheValidator {
    public:
        bool validate(const PipelineCacheHeader& header);
        bool validateVulkan(const PipelineCacheValidationInfo& validator, const PipelineCacheHeader& header);
    private:
        bool readyForVulkanValidationStep = false;
    };
}
