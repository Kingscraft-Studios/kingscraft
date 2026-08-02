#pragma once

#include "Core/Diagnostics/Metrics/MetricsTypes.hpp"

namespace lve {

    class Metrics {
    public:
        void clear();

        GameLogicMetrics& cpu();
        RenderThreadMetrics& gpu();

        FrameMetrics snapshot() const;

    private:
        FrameMetrics current_;
    };

}
