#pragma once

#include <cstdint>

namespace lve {

    struct PipelineStats {
        uint64_t iaVertices = 0;
        uint64_t iaPrimitives = 0;
        uint64_t vsInvocations = 0;
        uint64_t fsInvocations = 0;
        uint64_t clipInvocations = 0;
        uint64_t clipPrims = 0;
    };

    struct GameLogicMetrics {
        double delta = 0.0;
        double tickMs = 0.0;
        double frustumMs = 0.0;
        double drawMs = 0.0;
        uint32_t visibleChunks = 0;
        uint32_t visibleSubChunks = 0;
        uint32_t occlusionTested = 0;
        uint32_t occlusionRemoved = 0;
    };

    struct RenderThreadMetrics {
        double cpuFrameMs = 0.0;
        double cpuSubmitMs = 0.0;
        double cmdRecordMs = 0.0;
        double gpuFrameMs = 0.0;
        double worldGpuMs = 0.0;
        double uiGpuMs = 0.0;
        double memBandwidthGBs = 0.0;
        double overdraw = 0.0;
        PipelineStats pipeline;
        uint64_t frames = 0;
        double cIdle = 0.0;
    };

    struct FrameMetrics {
        GameLogicMetrics cpu;
        RenderThreadMetrics gpu;
    };

}
