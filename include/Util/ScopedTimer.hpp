#pragma once

#include "Util/TimeUtil.hpp"

namespace kc {

    // Times a scope and adds elapsed ms into the given accumulator on exit.
    class ScopedTimer {
    public:
        explicit ScopedTimer(double& accumulatorMs)
            : acc_(accumulatorMs), start_(TimeUtil::uptimeSeconds()) {}
        ~ScopedTimer() { acc_ += (TimeUtil::uptimeSeconds() - start_) * 1000.0; }
        double elapsedMs() const { return (TimeUtil::uptimeSeconds() - start_) * 1000.0; }
        ScopedTimer(const ScopedTimer&) = delete;
        ScopedTimer& operator=(const ScopedTimer&) = delete;

    private:
        double& acc_;
        double start_;
    };

}
