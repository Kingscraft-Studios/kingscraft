#include "UI/Debug/ProfilingCapture.hpp"
#include "Vulkan/Window.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <cmath>

#include "Bus/MessageBus.hpp"
#include "Threads/InputThread.hpp"

namespace lve {

    void ProfilingCapture::start() {
        cpuFrameMs_.clear();
        gpuTotalMs_.clear();
        worldGpuMs_.clear();
        uiGpuMs_.clear();
        cpuTickMs_.clear();
        cpuSubmitMs_.clear();
        cmdRecordMs_.clear();
        frustumMs_.clear();
        drawMs_.clear();
        memBwGBs_.clear();
        overdraw_.clear();
        iaVerts_.clear();
        iaPrims_.clear();
        vsInvoc_.clear();
        fsInvoc_.clear();
        clipPrims_.clear();
        visibleChunks_.clear();
        visibleSubChunks_.clear();
        occlusionTested_.clear();
        occlusionRemoved_.clear();

        elapsed_ = 0.0;
        frameCount_ = 0;
        active_ = true;
    }

    void ProfilingCapture::tick(double dt) {
        if (!active_) return;

        elapsed_ += dt;
        if (elapsed_ >= CAPTURE_DURATION) {
            stopAndCopy();
        }
    }

    void ProfilingCapture::feedFrame(const FrameMetrics& frame)
    {
        if (!active_) return;

        cpuFrameMs_.push_back(frame.gpu.cpuFrameMs);
        gpuTotalMs_.push_back(frame.gpu.gpuFrameMs);
        worldGpuMs_.push_back(frame.gpu.worldGpuMs);
        uiGpuMs_.push_back(frame.gpu.uiGpuMs);
        cpuTickMs_.push_back(frame.cpu.tickMs);
        cpuSubmitMs_.push_back(frame.gpu.cpuSubmitMs);
        cmdRecordMs_.push_back(frame.gpu.cmdRecordMs);
        frustumMs_.push_back(frame.cpu.frustumMs);
        drawMs_.push_back(frame.cpu.drawMs);
        memBwGBs_.push_back(frame.gpu.memBandwidthGBs);
        overdraw_.push_back(frame.gpu.overdraw);
        iaVerts_.push_back(frame.gpu.pipeline.iaVertices);
        iaPrims_.push_back(frame.gpu.pipeline.iaPrimitives);
        vsInvoc_.push_back(frame.gpu.pipeline.vsInvocations);
        fsInvoc_.push_back(frame.gpu.pipeline.fsInvocations);
        clipPrims_.push_back(frame.gpu.pipeline.clipPrims);
        visibleChunks_.push_back(frame.cpu.visibleChunks);
        visibleSubChunks_.push_back(frame.cpu.visibleSubChunks);
        occlusionTested_.push_back(frame.cpu.occlusionTested);
        occlusionRemoved_.push_back(frame.cpu.occlusionRemoved);

        frameCount_++;
    }

    template<typename T>
    struct Stats {
        double avg = 0.0;
        T min = T{};
        T max = T{};
    };

    template<typename T>
    static Stats<T> computeStats(const std::vector<T>& data) {
        Stats<T> s;
        if (data.empty()) return s;
        s.min = data[0];
        s.max = data[0];
        double sum = 0.0;
        for (const auto& v : data) {
            sum += static_cast<double>(v);
            if (v < s.min) s.min = v;
            if (v > s.max) s.max = v;
        }
        s.avg = sum / static_cast<double>(data.size());
        return s;
    }

    void ProfilingCapture::stopAndCopy() {
        active_ = false;

        if (frameCount_ < 2) return;

        auto sFrame    = computeStats(cpuFrameMs_);
        auto sGpu      = computeStats(gpuTotalMs_);
        auto sWorld    = computeStats(worldGpuMs_);
        auto sUi       = computeStats(uiGpuMs_);
        auto sTick     = computeStats(cpuTickMs_);
        auto sSubmit   = computeStats(cpuSubmitMs_);
        auto sCmd      = computeStats(cmdRecordMs_);
        auto sFrustum  = computeStats(frustumMs_);
        auto sDraw     = computeStats(drawMs_);
        auto sBw       = computeStats(memBwGBs_);
        auto sOd       = computeStats(overdraw_);
        auto sVerts    = computeStats(iaVerts_);
        auto sPrims    = computeStats(iaPrims_);
        auto sVs       = computeStats(vsInvoc_);
        auto sFs       = computeStats(fsInvoc_);
        auto sClip     = computeStats(clipPrims_);
        auto sChunks   = computeStats(visibleChunks_);
        auto sSubChunks = computeStats(visibleSubChunks_);
        auto sOcclTested = computeStats(occlusionTested_);
        auto sOcclRemoved = computeStats(occlusionRemoved_);

        char buf[4096];
        int pos = 0;

        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "=== Profiling Capture (%.1fs, %d frames) ===\n",
            CAPTURE_DURATION, frameCount_);

        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "%-22s %-12s %-12s %-12s\n", "Metric", "Avg", "Min", "Max");

        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "--------------------------------------------------------\n");

        auto line = [&](const char* name, double avg, double min, double max, const char* fmt) {
            char val[128];
            snprintf(val, sizeof(val), fmt, avg, min, max);
            pos += snprintf(buf + pos, sizeof(buf) - pos, "%-22s %s\n", name, val);
        };

        line("Frame (ms)",            sFrame.avg,   sFrame.min,   sFrame.max,   "%-12.3f %-12.3f %-12.3f");
        line("GPU Total (ms)",        sGpu.avg,     sGpu.min,     sGpu.max,     "%-12.3f %-12.3f %-12.3f");
        line("World GPU (ms)",        sWorld.avg,   sWorld.min,   sWorld.max,   "%-12.3f %-12.3f %-12.3f");
        line("UI GPU (ms)",           sUi.avg,      sUi.min,      sUi.max,      "%-12.3f %-12.3f %-12.3f");
        line("CPU Tick (ms)",         sTick.avg,    sTick.min,    sTick.max,    "%-12.3f %-12.3f %-12.3f");
        line("CPU Submit (ms)",       sSubmit.avg,  sSubmit.min,  sSubmit.max,  "%-12.3f %-12.3f %-12.3f");
        line("Cmd Record (ms)",       sCmd.avg,     sCmd.min,     sCmd.max,     "%-12.3f %-12.3f %-12.3f");
        line("Frustum (ms)",          sFrustum.avg, sFrustum.min, sFrustum.max, "%-12.3f %-12.3f %-12.3f");
        line("Draw (ms)",             sDraw.avg,    sDraw.min,    sDraw.max,    "%-12.3f %-12.3f %-12.3f");
        line("Memory BW (GB/s)",      sBw.avg,      sBw.min,      sBw.max,      "%-12.3f %-12.3f %-12.3f");
        line("Overdraw (x)",          sOd.avg,      sOd.min,      sOd.max,      "%-12.3f %-12.3f %-12.3f");

        line("IA Vertices (K)",       sVerts.avg / 1e3, sVerts.min / 1e3, sVerts.max / 1e3, "%-12.3f %-12.3f %-12.3f");
        line("IA Primitives (K)",     sPrims.avg / 1e3, sPrims.min / 1e3, sPrims.max / 1e3, "%-12.3f %-12.3f %-12.3f");
        line("VS Invocations (K)",    sVs.avg / 1e3,    sVs.min / 1e3,    sVs.max / 1e3,    "%-12.3f %-12.3f %-12.3f");
        line("FS Invocations (M)",    sFs.avg / 1e6,    sFs.min / 1e6,    sFs.max / 1e6,    "%-12.3f %-12.3f %-12.3f");
        line("Clipped Prims (K)",     sClip.avg / 1e3,  sClip.min / 1e3,  sClip.max / 1e3,  "%-12.3f %-12.3f %-12.3f");

        {
            char val[128];
            snprintf(val, sizeof(val), "%-12u %-12u %-12u",
                     static_cast<unsigned>(std::round(sChunks.avg)),
                     sChunks.min, sChunks.max);
            pos += snprintf(buf + pos, sizeof(buf) - pos, "%-22s %s\n", "Visible Chunks", val);
        }

        {
            char val[128];
            snprintf(val, sizeof(val), "%-12u %-12u %-12u",
                     static_cast<unsigned>(std::round(sSubChunks.avg)),
                     sSubChunks.min, sSubChunks.max);
            pos += snprintf(buf + pos, sizeof(buf) - pos, "%-22s %s\n", "Visible SubChunks", val);
        }

        {
            char val[128];
            snprintf(val, sizeof(val), "%-12u %-12u %-12u",
                     static_cast<unsigned>(std::round(sOcclTested.avg)),
                     sOcclTested.min, sOcclTested.max);
            pos += snprintf(buf + pos, sizeof(buf) - pos, "%-22s %s\n", "Occlusion Tested", val);
        }

        {
            char val[128];
            snprintf(val, sizeof(val), "%-12u %-12u %-12u",
                     static_cast<unsigned>(std::round(sOcclRemoved.avg)),
                     sOcclRemoved.min, sOcclRemoved.max);
            pos += snprintf(buf + pos, sizeof(buf) - pos, "%-22s %s\n", "Occlusion Removed", val);
        }

        if (pos >= (int)sizeof(buf)) pos = sizeof(buf) - 1;

        MessageBus::Get().send(ThreadName::Input, [buf]() {
             InputThread::getInstance().setClipBoard(buf);
        });
    }

} // namespace lve
