#pragma once

namespace kc {

    // Higher value = runs earlier. Dispatchers execute handlers in
    // highest-priority-first order, so Highest runs before High before Normal
    // ... Monitor runs last.
    enum class EventPriority {
        Lowest,
        Low,
        Normal,
        High,
        Highest,
        Monitor
    };
}