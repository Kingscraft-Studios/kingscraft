#pragma once

#include <vector>
#include <cstdint>

namespace lve {

    class ProfilingCapture {
    public:
        static constexpr double CAPTURE_DURATION = 3.0;

        void start();
        void tick(double dt);
        void feedFrame(
            double cpuFrameMs, double gpuTotalMs,
            double worldGpuMs, double uiGpuMs,
            double cpuTickMs, double cpuSubmitMs, double cmdRecordMs,
            double frustumMs, double drawMs,
            double memBwGBs, double overdraw,
            uint64_t iaVerts, uint64_t iaPrims,
            uint64_t vsInvoc, uint64_t fsInvoc,
            uint64_t clipPrims,
            uint32_t visibleChunks);

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
    };

} // namespace lve
