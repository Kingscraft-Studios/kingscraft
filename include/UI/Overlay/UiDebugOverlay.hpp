#pragma once

#include "UI/Overlay/UiOverlay.hpp"
#include "UI/Elements/UiTextBlock.hpp"
#include "UI/Elements/UiGroup.hpp"
#include "Util/TimeUtil.hpp"
#include "Core/Diagnostics/Metrics/MetricsTypes.hpp"

#include <atomic>
#include <memory>

namespace kc {

    class UiWrapper;

    class UiDebugOverlay : public UiOverlay {
    public:
        void init(UiWrapper& ui, float screenW, float screenH) override;
        void update(UiWrapper& ui);
        void setFrame(const FrameMetrics& frame);
        void cleanup(UiWrapper& ui) override;

    private:
        // Shared-alive token for the render-thread diagnostics dispatcher. The
        // dispatcher lambda captures a copy of this shared_ptr; cleanup() flips
        // it to false BEFORE freeing the overlay so a queued/in-flight callback
        // becomes a no-op instead of touching a destroyed overlay at shutdown.
        std::shared_ptr<std::atomic<bool>> alive_;

        UiGroup group_;
        UiTextBlock line1_;
        UiTextBlock line2_;
        UiTextBlock line3_;
        UiTextBlock line4_;
        uint32_t styleIndex_ = 0;
        double lastTime_ = 0.0;
        double elapsed_ = 0.0;
        int frameCount_ = 0;
        uint64_t latestFrames_ = 0;
        uint64_t prevFrames_ = 0;
        double latestCIdle_ = 0.0;

        double latestCpuMs_ = 0.0;
        double latestGpuMs_ = 0.0;
        double latestWorldGpuMs_ = 0.0;
        double latestUiGpuMs_ = 0.0;
        double latestCpuTickMs_ = 0.0;
        double latestCpuSubmitMs_ = 0.0;
        double latestCmdRecordMs_ = 0.0;
        double latestFrustumMs_ = 0.0;
        double latestDrawMs_ = 0.0;
        double latestMemBwGBs_ = 0.0;
        double latestOverdraw_ = 0.0;
        uint64_t latestIaVerts_ = 0;
        uint64_t latestIaPrims_ = 0;
        uint64_t latestVsInvoc_ = 0;
        uint64_t latestFsInvoc_ = 0;
        uint64_t latestClipPrims_ = 0;
        uint32_t latestVisibleChunks_ = 0;
        uint32_t latestVisibleSubChunks_ = 0;
        uint32_t latestOcclusionTested_ = 0;
        uint32_t latestOcclusionRemoved_ = 0;
    };

} // namespace kc