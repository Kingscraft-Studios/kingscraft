#include "UI/Debug/ProfilingCapture.hpp"
#include "Vulkan/GLFWWindow.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include "Bus/MessageBus.hpp"
#include "Core/Runtime.hpp"
#include "Renderer/RendererSettings.hpp"
#include "Threads/InputThread.hpp"
#include "Util/LogUtils.hpp"

namespace kc {

    void ProfilingCapture::start() {
        frameIntervalMs_.clear();
        cIdleMs_.clear();
        presentMs_.clear();
        logicDeltaMs_.clear();
        cpuFrameMs_.clear();
        gpuTotalMs_.clear();
        worldGpuMs_.clear();
        uiGpuMs_.clear();
        opaqueGpuMs_.clear();
        transparentGpuMs_.clear();
        terrainGpuMs_.clear();
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

        startTime_ = std::chrono::steady_clock::now();
        lastRenderedFrames_ = 0;
        firstRenderedFrames_ = 0;
        skippedFrames_ = 0;
        intervalSumMs_ = 0.0;
        warmupRemaining_ = WARMUP_FRAMES;
        frameCount_ = 0;
        active_.store(true, std::memory_order_release);
    }

    void ProfilingCapture::tick(double) {
        if (!isActive()) return;

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration<double>(now - startTime_).count() >= CAPTURE_DURATION)
            stopAndCopy();
    }

    void ProfilingCapture::feedFrame(const FrameMetrics& frame)
    {
        if (!isActive()) return;

        // MetricsUpdateEvent fires per game-loop iteration, which is often more
        // than once per rendered frame. Only take one sample per rendered frame.
        if (frame.gpu.frames == lastRenderedFrames_) return;
        lastRenderedFrames_ = frame.gpu.frames;

        if (warmupRemaining_ > 0) {
            --warmupRemaining_;
            return;
        }

        // First frame the capture actually keeps; the render thread's counter is
        // monotonic, so last - first is the number of frames it drew.
        if (firstRenderedFrames_ == 0) firstRenderedFrames_ = frame.gpu.frames;
        skippedFrames_ = frame.gpu.skippedFrames;

        frameIntervalMs_.push_back(frame.gpu.frameIntervalMs);
        mailboxMs_.push_back(frame.gpu.mailboxMs);
        beginMs_.push_back(frame.gpu.beginMs);
        workMs_.push_back(frame.gpu.workMs);
        gapMs_.push_back(frame.gpu.gapMs);
        // Tail after the queue submit: collectDestroyedChunks, the profiling
        // tick, the max-Fps sleep and the metrics publish. Computed on the
        // render thread, so it describes the same frame as the other parts.
        postMs_.push_back(frame.gpu.postMs);
        intervalSumMs_ += frame.gpu.frameIntervalMs;
        cIdleMs_.push_back(frame.gpu.cIdle);
        presentMs_.push_back(frame.gpu.presentMs);
        logicDeltaMs_.push_back(frame.cpu.delta * 1000.0);
        cpuFrameMs_.push_back(frame.gpu.cpuFrameMs);
        gpuTotalMs_.push_back(frame.gpu.gpuFrameMs);
        worldGpuMs_.push_back(frame.gpu.worldGpuMs);
        uiGpuMs_.push_back(frame.gpu.uiGpuMs);
        opaqueGpuMs_.push_back(frame.gpu.opaqueGpuMs);
        transparentGpuMs_.push_back(frame.gpu.transparentGpuMs);
        terrainGpuMs_.push_back(frame.gpu.terrainGpuMs);
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

    template<typename T>
    static double percentile(std::vector<T> data, double p) {
        if (data.empty()) return 0.0;
        std::sort(data.begin(), data.end());
        const size_t idx = static_cast<size_t>(p * static_cast<double>(data.size() - 1));
        return static_cast<double>(data[idx]);
    }

    void ProfilingCapture::stopAndCopy() {
        active_.store(false, std::memory_order_release);

        if (frameCount_ < 2) return;

        const double wallSec =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime_).count();
        // Samples per wall second. This understates the render rate whenever a
        // metrics publish is dropped and the capture never sees that frame at
        // all, so it is reported next to the interval-derived rate below.
        const double fpsSampled = wallSec > 0.0 ? frameCount_ / wallSec : 0.0;

        auto sInterval = computeStats(frameIntervalMs_);
        auto sMailbox  = computeStats(mailboxMs_);
        auto sBegin    = computeStats(beginMs_);
        auto sWork     = computeStats(workMs_);
        auto sGap      = computeStats(gapMs_);
        auto sPost     = computeStats(postMs_);
        auto sCIdle    = computeStats(cIdleMs_);
        auto sPresent  = computeStats(presentMs_);
        auto sLogic    = computeStats(logicDeltaMs_);
        auto sOpaque   = computeStats(opaqueGpuMs_);
        auto sTrans    = computeStats(transparentGpuMs_);
        auto sTerrain  = computeStats(terrainGpuMs_);
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

        const double fpsInterval = sInterval.avg > 0.0 ? 1000.0 / sInterval.avg : 0.0;

        // How many frames the render thread actually drew while the capture was
        // running, versus how many of them the profiler was handed.
        const uint64_t renderedFrames =
            (firstRenderedFrames_ > 0 && lastRenderedFrames_ >= firstRenderedFrames_)
                ? lastRenderedFrames_ - firstRenderedFrames_ + 1
                : static_cast<uint64_t>(frameCount_);
        const double samplePct = renderedFrames > 0
            ? 100.0 * static_cast<double>(frameCount_) / static_cast<double>(renderedFrames)
            : 0.0;
        // Frames the render thread actually completed per wall second. This is
        // the honest average: the profiler is only handed one sample per game
        // logic iteration, so it sees a subset of frames when logic runs slower
        // than the renderer.
        const double fpsRendered =
            wallSec > 0.0 ? static_cast<double>(renderedFrames) / wallSec : 0.0;

        // Fraction of the capture's wall time that sampled frame intervals
        // actually account for. 100% means every rendered frame was seen.
        const double coveragePct =
            wallSec > 0.0 ? 100.0 * (intervalSumMs_ / 1000.0) / wallSec : 0.0;

        char buf[8192];
        int pos = 0;

        const auto& rs = RendererSettings::get();
        const auto extent = Runtime::get().inputThread->getExtent();

        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "=== Profiling Capture (%.1fs wall, %llu frames, %.1f FPS) ===\n",
            wallSec, static_cast<unsigned long long>(renderedFrames), fpsRendered);

        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "Window %ux%u | RD %d | worldHeight %d | vsync %d | maxFps %d | warmup %d\n",
            static_cast<unsigned>(extent.width), static_cast<unsigned>(extent.height),
            rs.renderDistance, rs.worldHeight, rs.vsync ? 1 : 0, rs.maxFps, WARMUP_FRAMES);

        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "%-22s %-12s %-12s %-12s\n", "Metric", "Avg", "Min", "Max");

        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "--------------------------------------------------------\n");

        auto line = [&](const char* name, double avg, double min, double max, const char* fmt) {
            char val[128];
            snprintf(val, sizeof(val), fmt, avg, min, max);
            pos += snprintf(buf + pos, sizeof(buf) - pos, "%-22s %s\n", name, val);
        };

        line("Frame Interval (ms)",   sInterval.avg, sInterval.min, sInterval.max, "%-12.3f %-12.3f %-12.3f");
        line("Work (ms)",             sWork.avg,     sWork.min,     sWork.max,     "%-12.3f %-12.3f %-12.3f");
        line("Loop Gap (ms)",         sGap.avg,      sGap.min,      sGap.max,      "%-12.3f %-12.3f %-12.3f");
        line("Mailbox (ms)",          sMailbox.avg,  sMailbox.min,  sMailbox.max,  "%-12.3f %-12.3f %-12.3f");
        line("Begin (ms)",            sBegin.avg,    sBegin.min,    sBegin.max,    "%-12.3f %-12.3f %-12.3f");
        line("Frame (ms)",            sFrame.avg,   sFrame.min,   sFrame.max,   "%-12.3f %-12.3f %-12.3f");
        line("GPU Total (ms)",        sGpu.avg,     sGpu.min,     sGpu.max,     "%-12.3f %-12.3f %-12.3f");
        line("World GPU (ms)",        sWorld.avg,   sWorld.min,   sWorld.max,   "%-12.3f %-12.3f %-12.3f");
        line("Terrain GPU (ms)",      sTerrain.avg, sTerrain.min, sTerrain.max, "%-12.3f %-12.3f %-12.3f");
        line("Opaque GPU (ms)",       sOpaque.avg,  sOpaque.min,  sOpaque.max,  "%-12.3f %-12.3f %-12.3f");
        line("Water GPU (ms)",        sTrans.avg,   sTrans.min,   sTrans.max,   "%-12.3f %-12.3f %-12.3f");
        line("UI GPU (ms)",           sUi.avg,      sUi.min,      sUi.max,      "%-12.3f %-12.3f %-12.3f");
        line("CIdle (ms)",            sCIdle.avg,   sCIdle.min,   sCIdle.max,   "%-12.3f %-12.3f %-12.3f");
        line("Present (ms)",          sPresent.avg, sPresent.min, sPresent.max, "%-12.3f %-12.3f %-12.3f");
        line("CPU Tick (ms)",         sTick.avg,    sTick.min,    sTick.max,    "%-12.3f %-12.3f %-12.3f");
        line("CPU Submit (ms)",       sSubmit.avg,  sSubmit.min,  sSubmit.max,  "%-12.3f %-12.3f %-12.3f");
        line("Cmd Record (ms)",       sCmd.avg,     sCmd.min,     sCmd.max,     "%-12.3f %-12.3f %-12.3f");
        line("Post-Submit (ms)",      sPost.avg,    sPost.min,    sPost.max,    "%-12.3f %-12.3f %-12.3f");
        line("Frustum (ms)",          sFrustum.avg, sFrustum.min, sFrustum.max, "%-12.3f %-12.3f %-12.3f");
        line("Draw (ms)",             sDraw.avg,    sDraw.min,    sDraw.max,    "%-12.3f %-12.3f %-12.3f");
        line("Logic Loop (ms)",       sLogic.avg,   sLogic.min,   sLogic.max,   "%-12.3f %-12.3f %-12.3f");
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

        if (pos >= static_cast<int>(sizeof(buf))) pos = sizeof(buf) - 1;

        const double medianInterval = percentile(frameIntervalMs_, 0.50);

        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "\nFPS                    %.1f  (rendered frames / wall)\n"
            "FPS (interval)         %.1f  (1000 / avg frame interval)\n"
            "Samples / Rendered     %d / %llu  (%.1f%%)\n"
            "Sampling               %.0f samples/s\n"
            "Coverage               %.1f%%  (sum interval %.3f s / %.3f s wall)\n"
            "Skipped (no draw)      %llu\n"
            "Frame p50/p95/p99      %.3f / %.3f / %.3f ms\n"
            "Logic iters/s          %.0f\n",
            fpsRendered,
            fpsInterval,
            frameCount_,
            static_cast<unsigned long long>(renderedFrames), samplePct,
            fpsSampled,
            coveragePct, intervalSumMs_ / 1000.0, wallSec,
            static_cast<unsigned long long>(skippedFrames_),
            medianInterval,
            percentile(frameIntervalMs_, 0.95),
            percentile(frameIntervalMs_, 0.99),
            sLogic.avg > 0.0 ? 1000.0 / sLogic.avg : 0.0);

        // Spike tail: tells us how much of the average FPS is lost to outliers
        // versus the steady-state cost.
        {
            size_t spikes = 0;
            for (double v : frameIntervalMs_)
                if (v > 2.0 * medianInterval) ++spikes;

            const double pct = frameIntervalMs_.empty()
                ? 0.0
                : 100.0 * static_cast<double>(spikes) /
                      static_cast<double>(frameIntervalMs_.size());

            if (pos >= static_cast<int>(sizeof(buf))) pos = sizeof(buf) - 1;
            pos += snprintf(buf + pos, sizeof(buf) - pos,
                "Spikes > 2x median     %zu (%.1f%%)\n", spikes, pct);
        }

        // Worst frames, fully broken down. This is what distinguishes a GPU
        // spike from a compositor stall (CIdle/Present) or CPU scheduling
        // (Cmd Record/Frustum/Logic).
        {
            constexpr size_t WORST_N = 5;
            std::vector<size_t> idx(frameIntervalMs_.size());
            for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;

            const size_t n = std::min(WORST_N, idx.size());
            std::partial_sort(idx.begin(), idx.begin() + static_cast<ptrdiff_t>(n), idx.end(),
                [this](size_t a, size_t b) {
                    return frameIntervalMs_[a] > frameIntervalMs_[b];
                });

            if (pos >= static_cast<int>(sizeof(buf))) pos = sizeof(buf) - 1;
            pos += snprintf(buf + pos, sizeof(buf) - pos,
                "\nWorst frames           interval / GPU / Begin / Frame / Submit / Post / Gap / Logic\n");
            for (size_t k = 0; k < n; ++k) {
                const size_t i = idx[k];
                if (pos >= static_cast<int>(sizeof(buf))) pos = sizeof(buf) - 1;
                pos += snprintf(buf + pos, sizeof(buf) - pos,
                    "  #%zu  %8.3f / %6.3f / %6.3f / %6.3f / %6.3f / %6.3f / %6.3f / %6.3f ms\n",
                    k + 1,
                    frameIntervalMs_[i], gpuTotalMs_[i], beginMs_[i],
                    cpuFrameMs_[i], cpuSubmitMs_[i], postMs_[i],
                    gapMs_[i], logicDeltaMs_[i]);
            }
        }

        if (pos >= static_cast<int>(sizeof(buf))) pos = sizeof(buf) - 1;
        const std::string report(buf, static_cast<size_t>(pos));

        // Always persist: the Wayland clipboard can silently drop the selection.
        const char* home = std::getenv("HOME");
        const std::string path =
            std::string(home ? home : ".") + "/kingscraft-profile.txt";
        {
            std::ofstream out(path, std::ios::trunc);
            if (out) out << report;
        }
        LogUtils::info(ThreadName::GameLogic,
            "profile capture done (" + std::to_string(frameCount_) +
            " frames, " + std::to_string(static_cast<int>(wallSec)) + "s) -> " + path);

        MessageBus::Get().send(ThreadName::Input, [report]() {
            Runtime::get().inputThread->setClipBoard(report);
        });
    }

} // namespace kc
