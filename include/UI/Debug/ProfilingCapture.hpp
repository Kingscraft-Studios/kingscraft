#pragma once

#include <vector>
#include <cstdint>

#include "Core/Diagnostics/Metrics/MetricsTypes.hpp"

namespace kc {

    class ProfilingCapture {
    public:
        static constexpr double CAPTURE_DURATION = 3.0;

        void start();
        void tick(double dt);
        void feedFrame(const FrameMetrics& frame);

        bool isActive() const { return active_; }

    private:
        void stopAndCopy();

        bool active_ = false;
        double elapsed_ = 0.0;
        int frameCount_ = 0;

        std::vector<double> cpuFrameMs_, gpuTotalMs_, worldGpuMs_, uiGpuMs_;
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
