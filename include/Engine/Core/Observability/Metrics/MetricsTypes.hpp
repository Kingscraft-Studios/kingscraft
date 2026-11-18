#pragma once

#include <cstdint>

namespace kc {

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
        double mailboxMs = 0.0;        // tick entry -> drawFrame entry (scene read, resize checks)
        double beginMs = 0.0;          // drawFrame entry -> beginFrame() returned (CIdle is inside)
        double workMs = 0.0;           // whole frame on the render thread, tick entry -> end
        double gapMs = 0.0;            // previous frame end -> this frame start (loop overhead)
        double postMs = 0.0;           // workMs - mailbox - begin - frame - submit (post-submit tail)
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
        // Frames that produced no draw at all (paused, or the swapchain had to
        // be recreated after beginFrame() failed). These do not advance
        // `frames`, so the profiler can tell "few frames" from "lost frames".
        uint64_t skippedFrames = 0;
        double cIdle = 0.0;
        double frameIntervalMs = 0.0;  // wall time between rendered frames, render thread
        double presentMs = 0.0;        // vkQueuePresentKHR
        double opaqueGpuMs = 0.0;
        double transparentGpuMs = 0.0;
        double terrainGpuMs = 0.0;
    };

    struct FrameMetrics {
        GameLogicMetrics cpu;
        RenderThreadMetrics gpu;
    };

}
