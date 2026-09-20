#include "UI/Overlay/UiDebugOverlay.hpp"
#include "UI/UiWrapper.hpp"
#include "UI/Engine/UiStyle.hpp"
#include "Threads/Engine.hpp"
#include "Threads/Kingscraft.hpp"
#include "Core/Runtime.hpp"
#include "Core/Registry.hpp"

namespace kc {

    void UiDebugOverlay::init(UiWrapper& ui, float screenW, float screenH) {
        (void)screenW;
        (void)screenH;

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
        line1_.setName("DebugLine1");
        group_.add(&line1_);

        line2_.setAnchor({0.0f, 0.03f}, {10.0f, 10.0f});
        line2_.setNormalizedSize({0.65f, 0.035f});
        line2_.setFontSize(18.0f);
        line2_.setText("Verts: --  Tris: --  Ch: --  FS: --  OD: --  BW: --");
        line2_.setFont("default");
        line2_.setColor({0.6f, 1.0f, 0.6f, 1.0f});
        line2_.setStyleIndex(styleIndex_);
        line2_.setName("DebugLine2");
        group_.add(&line2_);

        line3_.setAnchor({0.0f, 0.055f}, {10.0f, 10.0f});
        line3_.setNormalizedSize({0.65f, 0.035f});
        line3_.setFontSize(18.0f);
        line3_.setText("CPU: --  [Cmd:--  Draw:--  Sub:--]  Idle:--  Clip:--");
        line3_.setFont("default");
        line3_.setColor({0.7f, 0.7f, 1.0f, 1.0f});
        line3_.setStyleIndex(styleIndex_);
        line3_.setName("DebugLine3");
        group_.add(&line3_);

        line4_.setAnchor({0.0f, 0.08f}, {10.0f, 10.0f});
        line4_.setNormalizedSize({0.65f, 0.035f});
        line4_.setFontSize(18.0f);
        line4_.setText("Biome: --");
        line4_.setFont("default");
        line4_.setColor({0.6f, 1.0f, 1.0f, 1.0f});
        line4_.setStyleIndex(styleIndex_);
        line4_.setName("DebugLine4");
        group_.add(&line4_);

        group_.addToWrapper(ui);

        lastTime_ = TimeUtil::uptimeSeconds();

        // Per-frame metrics feed (Renderer thread). Stays registered for the
        // lifetime of this overlay and is released in cleanup(). The lambda
        // captures the shared-alive token so a callback already queued in the
        // renderer mailbox cannot touch the overlay after it is torn down.
        alive_ = std::make_shared<std::atomic<bool>>(true);
        Engine::Get().getDiagnostics().setDispatcher(ThreadName::Renderer, 4.0,
            [this, &ui, alive = alive_](const FrameMetrics& frame) {
                if (!alive->load(std::memory_order_acquire)) return;
                setFrame(frame);
                update(ui);
            });
    }

    void UiDebugOverlay::update(UiWrapper& ui) {
        UiGuard guard(ui);
        double now = TimeUtil::uptimeSeconds();
        double dt = now - lastTime_;
        lastTime_ = now;

        elapsed_ += dt;
        frameCount_ += (latestFrames_ > prevFrames_) ? static_cast<int>(latestFrames_ - prevFrames_) : 0;
        prevFrames_ = latestFrames_;

        if (elapsed_ >= 0.25) {
            float fps = (frameCount_ > 0) ? frameCount_ / static_cast<float>(elapsed_) : 0.0f;
            float ms = (frameCount_ > 0) ? (elapsed_ / static_cast<double>(frameCount_)) * 1000.0f : 0.0f;

            char buf[256];
            int n = snprintf(buf, sizeof(buf),
                "FPS: %.0f  Frame: %.1fms  GPU: %.1fms  [W:%.1f  UI:%.1f]",
                fps, ms, latestGpuMs_,
                latestWorldGpuMs_, latestUiGpuMs_);
            line1_.setText(std::string(buf, n));

            uint64_t fsMil = latestFsInvoc_ / 1000000;
            char buf2[256];
            int n2 = snprintf(buf2, sizeof(buf2),
                "Verts: %llu  Tris: %llu  Ch: %u/%u  FS: %lluM  OD: %.1fx  BW: %.1fGB/s",
                (unsigned long long)latestIaVerts_,
                (unsigned long long)latestIaPrims_,
                latestVisibleSubChunks_, latestVisibleChunks_,
                (unsigned long long)fsMil,
                latestOverdraw_, latestMemBwGBs_);
            line2_.setText(std::string(buf2, n2));

            double cpuTotal = latestCpuMs_ + latestCpuTickMs_ + latestCpuSubmitMs_;

            char buf3[256];
            int n3 = snprintf(buf3, sizeof(buf3),
                "CPU:%.1fms [Cmd:%.2f  Draw:%.1f  Sub:%.2f]  CIdle:%.1fms  Occl:%u/%u",
                cpuTotal, latestCmdRecordMs_, latestDrawMs_, latestCpuSubmitMs_,
                latestCIdle_, latestOcclusionRemoved_, latestOcclusionTested_);
            line3_.setText(std::string(buf3, n3));

            // Current biome under the player (published by the GameLogic thread).
            uint64_t enc = Runtime::get().kingscraft->getCurrentBiome();
            std::string biomeName;
            if (enc) biomeName = Registry<Biome>::getRegistry().getIdentifier(enc);
            auto sep = biomeName.find(':');
            if (sep != std::string::npos) biomeName = biomeName.substr(sep + 1);
            if (biomeName.empty()) biomeName = "--";
            std::string line4 = "Biome: " + biomeName;
            if (Runtime::get().kingscraft->isPlayerFlying()) line4 += "  [FLY]";
            line4_.setText(line4);

            elapsed_ = 0.0;
            frameCount_ = 0;
        }
    }

    void UiDebugOverlay::setFrame(const FrameMetrics& frame) {
        latestCpuMs_ = frame.gpu.cpuFrameMs;
        latestGpuMs_ = frame.gpu.gpuFrameMs;
        latestWorldGpuMs_ = frame.gpu.worldGpuMs;
        latestUiGpuMs_ = frame.gpu.uiGpuMs;
        latestCpuTickMs_ = frame.cpu.tickMs;
        latestCpuSubmitMs_ = frame.gpu.cpuSubmitMs;
        latestCmdRecordMs_ = frame.gpu.cmdRecordMs;
        latestFrustumMs_ = frame.cpu.frustumMs;
        latestDrawMs_ = frame.cpu.drawMs;
        latestMemBwGBs_ = frame.gpu.memBandwidthGBs;
        latestOverdraw_ = frame.gpu.overdraw;
        latestIaVerts_ = frame.gpu.pipeline.iaVertices;
        latestIaPrims_ = frame.gpu.pipeline.iaPrimitives;
        latestVsInvoc_ = frame.gpu.pipeline.vsInvocations;
        latestFsInvoc_ = frame.gpu.pipeline.fsInvocations;
        latestClipPrims_ = frame.gpu.pipeline.clipPrims;
        latestVisibleChunks_ = frame.cpu.visibleChunks;
        latestVisibleSubChunks_ = frame.cpu.visibleSubChunks;
        latestOcclusionTested_ = frame.cpu.occlusionTested;
        latestOcclusionRemoved_ = frame.cpu.occlusionRemoved;
        latestFrames_ = frame.gpu.frames;
        latestCIdle_ = frame.gpu.cIdle;
    }

    void UiDebugOverlay::cleanup(UiWrapper& ui) {
        // Invalidate the render-thread dispatch token FIRST so any callback
        // already queued in the renderer mailbox becomes a no-op before this
        // overlay (and its elements) are freed.
        if (alive_) alive_->store(false, std::memory_order_release);
        Engine::Get().getDiagnostics().clearDispatcher(ThreadName::Renderer);
        group_.removeFromWrapper(ui);
    }

} // namespace kc