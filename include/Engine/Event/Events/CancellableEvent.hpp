#pragma once
#include "Event.hpp"

namespace kc {

    class CancellableEvent : public Event {
    public:
        virtual ~CancellableEvent() = default;

        bool isCancelled() const noexcept { return cancelled; }
        void setCancelled(bool shouldCancelled) noexcept { cancelled = shouldCancelled; }

    private:
        bool cancelled = false;
    };
}
