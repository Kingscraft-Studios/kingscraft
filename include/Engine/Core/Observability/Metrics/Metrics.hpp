#pragma once

#include "Core/Observability/Metrics/MetricsTypes.hpp"

namespace kc {

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
