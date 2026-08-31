#pragma once

#include <functional>
#include <vector>

#include "Bus/Message.hpp"
#include "Core/Diagnostics/Metrics/Metrics.hpp"

namespace kc {

    class Diagnostics {
    public:
        void start();
        void update();
        void cleanup();

        void setDispatcher(ThreadName target, std::function<void(const FrameMetrics&)> cb);
        void clearDispatcher(ThreadName target);
        void send();

        GameLogicMetrics& getCPUMetrics() { return metrics_.cpu(); }
        RenderThreadMetrics& getGPUMetrics() { return metrics_.gpu(); }
        FrameMetrics snapshot() const { return metrics_.snapshot(); }

        double getFps() const { return fps_; }
        double getAvgFrameMs() const { return avgFrameMs_; }

    private:
        struct DispatcherEntry {
            ThreadName target;
            std::function<void(const FrameMetrics&)> cb;
        };

        Metrics metrics_;
        std::vector<DispatcherEntry> dispatchers_;
        double lastUpdateSec_ = 0.0;
        int frameCount_ = 0;
        double elapsed_ = 0.0;
        double fps_ = 0.0;
        double avgFrameMs_ = 0.0;
    };

}
