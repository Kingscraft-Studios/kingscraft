#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <vector>

#include "Core/Observability/Metrics/MetricsTypes.hpp"

namespace kc {

    class ProfilingCapture {
    public:
        static constexpr double CAPTURE_DURATION = 10.0;
        static constexpr int WARMUP_FRAMES = 30;

        void start();
        void tick(double dt);
        void feedFrame(const FrameMetrics& frame);

        bool isActive() const { return active_.load(std::memory_order_acquire); }

    private:
        void stopAndCopy();

        std::atomic<bool> active_{false};
        std::chrono::steady_clock::time_point startTime_{};
        uint64_t lastRenderedFrames_ = 0;
        // First/last value of the render thread's frame counter seen by the
        // profiler. The difference is how many frames were actually drawn; a
        // gap against the sample count means metrics publishes were dropped and
        // the capture under-samples the render thread.
        uint64_t firstRenderedFrames_ = 0;
        uint64_t skippedFrames_ = 0;
        double intervalSumMs_ = 0.0;
        int warmupRemaining_ = WARMUP_FRAMES;
        int frameCount_ = 0;

        std::vector<double> frameIntervalMs_, cIdleMs_, presentMs_, logicDeltaMs_;
        std::vector<double> mailboxMs_, beginMs_, workMs_, gapMs_, postMs_;
        std::vector<double> cpuFrameMs_, gpuTotalMs_, worldGpuMs_, uiGpuMs_;
        std::vector<double> opaqueGpuMs_, transparentGpuMs_;
        std::vector<double> terrainGpuMs_;
        std::vector<double> cpuTickMs_, cpuSubmitMs_, cmdRecordMs_;
        std::vector<double> frustumMs_, drawMs_;
        std::vector<double> memBwGBs_, overdraw_;
        std::vector<uint64_t> iaVerts_, iaPrims_, vsInvoc_, fsInvoc_, clipPrims_;
        std::vector<uint32_t> visibleChunks_;
        std::vector<uint32_t> visibleSubChunks_;
        std::vector<uint32_t> occlusionTested_;
        std::vector<uint32_t> occlusionRemoved_;
    };

} // namespace kc
