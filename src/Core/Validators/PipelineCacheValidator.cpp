#include "Core/Validators/PipelineCacheValidator.hpp"

#include <iostream>

namespace lve {
    bool PipelineCacheValidator::validate(const PipelineCacheHeader& header) {

        // TODO: add Logging
        // validate the Magic
        if (header.magic != PIPELINE_CACHE_MAGIC) {
            return false;
        }

        // validate the Pipeline Version
        if (header.version != PIPELINE_CACHE_VERSION) {
            return false;
        }

        // validate Engine Version
        // FIXME:
        if (header.engineVersion != 1) {
            return false;
        }

        readyForVulkanValidationStep = true;

        return true;
    }

    bool PipelineCacheValidator::validateVulkan(const PipelineCacheValidationInfo &validator, const PipelineCacheHeader& header) {
        // TODO: add Logging
        if (!readyForVulkanValidationStep) return false;

        // validate vendorID
        if (header.vendorID != validator.vendorID) {
            return false;
        }

        // validate DeviceID
        if (header.deviceID != validator.deviceID) {
            return false;
        }

        if (header.driverVersion != validator.driverVersion) {
            return false;
        }

        // validate Pipeline Cache UUID
        if (header.pipelineCacheUUID != validator.pipelineCacheUUID) {
            return false;
        }

        return true;
    }
}
