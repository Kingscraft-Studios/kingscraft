#pragma once
#include <array>
#include <cstdint>
#include <string>

#include "vulkan/vulkan_core.h"

namespace kc {
    constexpr uint32_t PIPELINE_CACHE_MAGIC = 0x4B504348; // "KPCH (Kingscraft Pipeline Cache Header)"
    constexpr uint32_t PIPELINE_CACHE_VERSION = 1;
    constexpr uint32_t ENGINE_CACHE_VERSION = 1;

    constexpr const char* PIPELINE_CACHE_FILENAME = "pipeline.cache";

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

        std::string deviceName;
        std::string driverName;
    };
}
