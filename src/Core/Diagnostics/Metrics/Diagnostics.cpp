#include "Core/Diagnostics/Diagnostics.hpp"

#include "Bus/MessageBus.hpp"
#include "Util/LogUtils.hpp"
#include "Util/TimeUtil.hpp"

namespace kc {

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

    void Diagnostics::setDispatcher(ThreadName target, double hz, std::function<void(const FrameMetrics&)> cb) {
        if (hz <= 0.0) {
            LogUtils::error(ThreadName::Engine, "Diagnostics dispatcher hz must be Positive!");
            return;
        }
        for (auto& entry : dispatchers_) {
            if (entry.target == target) {
                entry.hz = hz;
                entry.cb = std::move(cb);
                return;
            }
        }
        dispatchers_.push_back({target, hz, 0.0, std::move(cb)});
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

        double now = TimeUtil::uptimeSeconds();
        FrameMetrics snap = metrics_.snapshot();
        for (auto& entry : dispatchers_) {
            if (entry.hz <= 0.0 || !entry.cb) continue;
            if (now - entry.lastSendSec_ >= 1.0 / entry.hz) {
                entry.lastSendSec_ = now;
                MessageBus::Get().send(entry.target, [snap, cb = entry.cb]() { cb(snap); });
            }
        }
    }

    double Diagnostics::nextDispatchIn() const {
        double now = TimeUtil::uptimeSeconds();
        double next = 0.25;
        for (const auto& entry : dispatchers_) {
            if (entry.hz <= 0.0) continue;
            double dueIn = (1.0 / entry.hz) - (now - entry.lastSendSec_);
            if (dueIn < next) next = dueIn;
        }
        if (next < 0.001) next = 0.001;
        return next;
    }

}
