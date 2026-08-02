#include "Core/Diagnostics/Diagnostics.hpp"

#include "Bus/MessageBus.hpp"
#include "Util/TimeUtil.hpp"

namespace lve {

    void Diagnostics::start() {
        lastUpdateSec_ = TimeUtil::uptimeSeconds();
        frameCount_ = 0;
        elapsed_ = 0.0;
        fps_ = 0.0;
        avgFrameMs_ = 0.0;
        metrics_.clear();
        dispatchers_.clear();
    }

    void Diagnostics::update() {
        double now = TimeUtil::uptimeSeconds();
        double dt = now - lastUpdateSec_;
        lastUpdateSec_ = now;

        elapsed_ += dt;
        frameCount_++;

        if (elapsed_ >= 0.25) {
            fps_ = frameCount_ / static_cast<double>(elapsed_);
            avgFrameMs_ = (elapsed_ / static_cast<double>(frameCount_)) * 1000.0;
            elapsed_ = 0.0;
            frameCount_ = 0;
        }
    }

    void Diagnostics::cleanup() {
        lastUpdateSec_ = 0.0;
        frameCount_ = 0;
        elapsed_ = 0.0;
        fps_ = 0.0;
        avgFrameMs_ = 0.0;
        metrics_.clear();
        dispatchers_.clear();
    }

    void Diagnostics::setDispatcher(ThreadName target, std::function<void(const FrameMetrics&)> cb) {
        for (auto& entry : dispatchers_) {
            if (entry.target == target) {
                entry.cb = std::move(cb);
                return;
            }
        }
        dispatchers_.push_back({target, std::move(cb)});
    }

    void Diagnostics::clearDispatcher(ThreadName target) {
        for (auto it = dispatchers_.begin(); it != dispatchers_.end(); ++it) {
            if (it->target == target) {
                dispatchers_.erase(it);
                return;
            }
        }
    }

    void Diagnostics::send() {
        if (dispatchers_.empty()) return;

        FrameMetrics snap = metrics_.snapshot();
        for (const auto& entry : dispatchers_) {
            if (entry.cb) {
                MessageBus::Get().send(entry.target, [snap, cb = entry.cb]() { cb(snap); });
            }
        }
    }

}
