#pragma once
#include <array>
#include <cstdint>

#include "vulkan/vulkan_core.h"

namespace kc {

    constexpr uint32_t PIPELINE_CACHE_MAGIC = 0x4B504348; // "KPCH (Kingscraft Pipeline Cache Header)"
    constexpr uint32_t PIPELINE_CACHE_VERSION = 1;

    struct PipelineCacheHeader {
        uint32_t magic;
        uint32_t version;

        uint32_t vendorID;
        uint32_t deviceID;

        uint32_t driverVersion;

        std::array<uint8_t, VK_UUID_SIZE> pipelineCacheUUID;

        uint64_t engineVersion;
    };

    struct PipelineCacheValidationInfo {
        uint32_t vendorID;
        uint32_t deviceID;
        uint32_t driverVersion;

        std::array<uint8_t, VK_UUID_SIZE> pipelineCacheUUID;
    };
    class PipelineCacheValidator {
    public:
        bool validate(const PipelineCacheHeader& header);
        bool validateVulkan(const PipelineCacheValidationInfo& validator, const PipelineCacheHeader& header);
    private:
        bool readyForVulkanValidationStep = false;
    };
}
