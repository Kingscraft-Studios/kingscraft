#include "Core/Validators/PipelineCacheValidator.hpp"

#include "Util/LogUtils.hpp"

namespace kc {
    bool PipelineCacheValidator::validate(const PipelineCacheHeader& header) {

        // validate the Magic
        if (header.magic != PIPELINE_CACHE_MAGIC) {
            LogUtils::error(ThreadName::Engine, StringBuilder::build("[PCV] Failed: Invalid Magic. Expected: ", PIPELINE_CACHE_MAGIC));
            return false;
        }

        // validate the Pipeline Version
        if (header.version != PIPELINE_CACHE_VERSION) {
            LogUtils::error(ThreadName::Engine, StringBuilder::build("[PCV] Failed: Cache Version Mismatch. Expected: ", PIPELINE_CACHE_VERSION));
            return false;
        }

        // validate Engine Version
        // FIXME:
        if (header.engineVersion != 1) {
            LogUtils::error(ThreadName::Engine, StringBuilder::build("[PCV] Failed: Engine Version Mismatch. Expected: ", 1));
            return false;
        }

        readyForVulkanValidationStep = true;

        return true;
    }

    bool PipelineCacheValidator::validateVulkan(const PipelineCacheValidationInfo &validator, const PipelineCacheHeader& header) {
        if (!readyForVulkanValidationStep) {
            LogUtils::error(ThreadName::Engine, "[PCV] validateVulkan() called before validate()");
            return false;
        }

        // validate vendorID
        if (header.vendorID != validator.vendorID) {
            LogUtils::error(ThreadName::Engine, "[PCV] Rejected: Vendor ID mismatch");
            return false;
        }

        // validate DeviceID
        if (header.deviceID != validator.deviceID) {
            LogUtils::error(ThreadName::Engine, "[PCV] Rejected: Device ID mismatch");
            return false;
        }

        if (header.driverVersion != validator.driverVersion) {
            LogUtils::error(ThreadName::Engine, "[PCV] Rejected: Driver version mismatch");
            return false;
        }

        // validate Pipeline Cache UUID
        if (header.pipelineCacheUUID != validator.pipelineCacheUUID) {
            LogUtils::error(ThreadName::Engine, "[PCV] Rejected: Pipeline Cache UUID mismatch");
            return false;
        }

        LogUtils::info(ThreadName::Engine, "[PCV] Succeeded");
        return true;
    }
}
