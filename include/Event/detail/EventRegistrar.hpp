#pragma once
#include "TypeTag.hpp"
#include "Event/EventPriority.hpp"
#include "Event/Events/Event.hpp"
#include "Event/Listener.hpp"

namespace kc::detail {

    struct EventRegistrar {
        template<typename L, typename E>
        static constexpr bool handles() {
            return requires {
                L::template kcOnEvent<L>(*(Listener*)nullptr, *(Event*)nullptr,
                                         (TypeTag<E>*)nullptr);
            };
        }

        template<typename L, typename E>
        static constexpr EventPriority priority() {
            return L::kcPriority((TypeTag<E>*)nullptr);
        }
    };

}
