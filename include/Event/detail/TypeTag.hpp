#pragma once

namespace kc::detail {

    template<typename EventType>
    struct TypeTag {
        using type = EventType;
    };
}