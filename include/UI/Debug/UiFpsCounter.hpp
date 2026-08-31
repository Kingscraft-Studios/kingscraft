#pragma once

#include "UI/Elements/UiTextBlock.hpp"
#include "UI/Elements/UiGroup.hpp"
#include "Util/TimeUtil.hpp"
#include "Core/Diagnostics/Metrics/MetricsTypes.hpp"

namespace kc {

    class UiWrapper;

    class UiFpsCounter {
    public:
        void init(UiWrapper& ui);
        void update(UiWrapper& ui);
        void setFrame(const FrameMetrics& frame);
        void cleanup(UiWrapper& ui);

    private:
        UiGroup group_;
        UiTextBlock line1_;
        UiTextBlock line2_;
        UiTextBlock line3_;
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
