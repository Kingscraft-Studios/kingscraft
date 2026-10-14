#include "Cache/Validators/PipelineCacheValidator.hpp"

#include "Cache/Constants/PipelineCacheConstants.hpp"
#include "Util/LogUtils.hpp"
#include "Util/StringUtils.hpp"

namespace kc {
    bool PipelineCacheValidator::validate(const PipelineCacheHeader& header) {

        // validate the Magic
        if (header.magic != PIPELINE_CACHE_MAGIC) {
            LogUtils::error(ThreadName::Engine, StringUtils::build("[PipelineCache] Failed: Invalid Magic. Expected: ", PIPELINE_CACHE_MAGIC));
            return false;
        }

        // validate the Pipeline Version
        if (header.version != PIPELINE_CACHE_VERSION) {
            LogUtils::error(ThreadName::Engine, StringUtils::build("[PipelineCache] Failed: Cache Version Mismatch. Expected: ", PIPELINE_CACHE_VERSION));
            return false;
        }

        if (header.engineVersion != ENGINE_CACHE_VERSION) {
            LogUtils::error(ThreadName::Engine, StringUtils::build("[PipelineCache] Failed: Engine Version Mismatch. Expected: ", ENGINE_CACHE_VERSION));
            return false;
        }

        readyForVulkanValidationStep = true;

        return true;
    }

    bool PipelineCacheValidator::validateVulkan(const PipelineCacheValidationInfo &validator, const PipelineCacheHeader& header) {
        if (!readyForVulkanValidationStep) {
            LogUtils::error(ThreadName::Engine, "[PipelineCache] validateVulkan() called before validate()");
            return false;
        }

        // validate vendorID
        if (header.vendorID != validator.vendorID) {
            LogUtils::error(ThreadName::Engine, "[PipelineCache] Rejected: Vendor ID mismatch");
            return false;
        }

        // validate DeviceID
        if (header.deviceID != validator.deviceID) {
            LogUtils::error(ThreadName::Engine, "[PipelineCache] Rejected: Device ID mismatch");
            return false;
        }

        if (header.driverVersion != validator.driverVersion) {
            LogUtils::error(ThreadName::Engine, "[PipelineCache] Rejected: Driver version mismatch");
            return false;
        }

        // validate Pipeline Cache UUID
        if (header.pipelineCacheUUID != validator.pipelineCacheUUID) {
            LogUtils::error(ThreadName::Engine, "[PipelineCache] Rejected: Pipeline Cache UUID mismatch");
            return false;
        }

        LogUtils::info(ThreadName::Engine, StringUtils::build(
            "[PipelineCache] Validation passed (Device ", validator.deviceName,
            ", Driver ", validator.driverName,
            "), reusing cached pipelines."));
        return true;
    }
}
