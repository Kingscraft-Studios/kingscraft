#include "UI/Debug/UiFpsCounter.hpp"
#include "UI/UiWrapper.hpp"
#include "UI/Engine/UiStyle.hpp"

namespace lve {

    void UiFpsCounter::init(UiWrapper& ui) {
        styleIndex_ = ui.registerStyle(UiStyle{
            .mode = RenderMode::Font,
            .color1 = {1.0f, 1.0f, 1.0f, 1.0f}
        });

        line1_.setAnchor({0.0f, 0.0f}, {10.0f, 10.0f});
        line1_.setNormalizedSize({0.65f, 0.04f});
        line1_.setFontSize(24.0f);
        line1_.setText("FPS: --  Frame: --  GPU: --  [W:--  UI:--]");
        line1_.setFont("default");
        line1_.setColor({1.0f, 1.0f, 1.0f, 1.0f});
        line1_.setStyleIndex(styleIndex_);
        line1_.setName("FpsLine1");
        ui.addElement(&line1_);

        line2_.setAnchor({0.0f, 0.03f}, {10.0f, 10.0f});
        line2_.setNormalizedSize({0.65f, 0.035f});
        line2_.setFontSize(18.0f);
        line2_.setText("Verts: --  Tris: --  Ch: --  FS: --  OD: --  BW: --");
        line2_.setFont("default");
        line2_.setColor({0.6f, 1.0f, 0.6f, 1.0f});
        line2_.setStyleIndex(styleIndex_);
        line2_.setName("FpsLine2");
        ui.addElement(&line2_);

        line3_.setAnchor({0.0f, 0.055f}, {10.0f, 10.0f});
        line3_.setNormalizedSize({0.65f, 0.035f});
        line3_.setFontSize(18.0f);
        line3_.setText("CPU: --  [Cmd:--  Draw:--  Sub:--]  Idle:--  Clip:--");
        line3_.setFont("default");
        line3_.setColor({0.7f, 0.7f, 1.0f, 1.0f});
        line3_.setStyleIndex(styleIndex_);
        line3_.setName("FpsLine3");
        ui.addElement(&line3_);

        lastTime_ = TimeUtil::uptimeSeconds();
    }

    void UiFpsCounter::update() {
        double now = TimeUtil::uptimeSeconds();
        double dt = now - lastTime_;
        lastTime_ = now;

        elapsed_ += dt;
        frameCount_++;

        if (elapsed_ >= 0.25) {
            float fps = frameCount_ / static_cast<float>(elapsed_);
            float ms = (elapsed_ / static_cast<double>(frameCount_)) * 1000.0f;

            char buf[256];
            int n = snprintf(buf, sizeof(buf),
                "FPS: %.0f  Frame: %.1fms  GPU: %.1fms  [W:%.1f  UI:%.1f]",
                fps, ms, latestGpuMs_,
                latestWorldGpuMs_, latestUiGpuMs_);
            line1_.setText(std::string(buf, n));

            uint64_t fsMil = latestFsInvoc_ / 1000000;
            char buf2[256];
            int n2 = snprintf(buf2, sizeof(buf2),
                "Verts: %llu  Tris: %llu  Ch: %u  FS: %lluM  OD: %.1fx  BW: %.1fGB/s",
                (unsigned long long)latestIaVerts_,
                (unsigned long long)latestIaPrims_,
                latestVisibleChunks_,
                (unsigned long long)fsMil,
                latestOverdraw_, latestMemBwGBs_);
            line2_.setText(std::string(buf2, n2));

            double cpuTotal = latestCpuMs_ + latestCpuTickMs_ + latestCpuSubmitMs_;
            double wallMs = ms;
            double gpuIdleMs = wallMs - latestGpuMs_ - cpuTotal;
            if (gpuIdleMs < 0.0) gpuIdleMs = 0.0;

            char buf3[256];
            int n3 = snprintf(buf3, sizeof(buf3),
                "CPU:%.1fms [Cmd:%.2f  Draw:%.1f  Sub:%.2f]  Idle:%.1fms  Clip:%.1fK",
                cpuTotal, latestCmdRecordMs_, latestDrawMs_, latestCpuSubmitMs_,
                gpuIdleMs, (double)latestClipPrims_ / 1000.0);
            line3_.setText(std::string(buf3, n3));

            elapsed_ = 0.0;
            frameCount_ = 0;
        }
    }

    void UiFpsCounter::setCpuGpuTimes(
        double cpuFrameMs, double gpuTotalMs,
        double worldGpuMs, double uiGpuMs,
        double cpuTickMs, double cpuSubmitMs, double cmdRecordMs,
        double frustumMs, double drawMs,
        double memBwGBs, double overdraw,
        uint64_t iaVerts, uint64_t iaPrims,
        uint64_t vsInvoc, uint64_t fsInvoc,
        uint64_t clipPrims,
        uint32_t visibleChunks)
    {
        latestCpuMs_ = cpuFrameMs;
        latestGpuMs_ = gpuTotalMs;
        latestWorldGpuMs_ = worldGpuMs;
        latestUiGpuMs_ = uiGpuMs;
        latestCpuTickMs_ = cpuTickMs;
        latestCpuSubmitMs_ = cpuSubmitMs;
        latestCmdRecordMs_ = cmdRecordMs;
        latestFrustumMs_ = frustumMs;
        latestDrawMs_ = drawMs;
        latestMemBwGBs_ = memBwGBs;
        latestOverdraw_ = overdraw;
        latestIaVerts_ = iaVerts;
        latestIaPrims_ = iaPrims;
        latestVsInvoc_ = vsInvoc;
        latestFsInvoc_ = fsInvoc;
        latestClipPrims_ = clipPrims;
        latestVisibleChunks_ = visibleChunks;
    }

    void UiFpsCounter::cleanup(UiWrapper& ui) {
        ui.removeElement(&line1_);
        ui.removeElement(&line2_);
        ui.removeElement(&line3_);
    }

} // namespace lve
