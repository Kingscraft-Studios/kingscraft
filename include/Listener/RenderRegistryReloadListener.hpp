#pragma once

#include "Event/Events/RegistryReloadEvent.hpp"
#include "Event/KCEventHandler.hpp"
#include "Event/Listener.hpp"
#include "Event/detail/EventRegistrar.hpp"

namespace kc {

    // Renderer-side listener: rebuilds the texture array, world renderer, and
    // UI block texture after a registry reload. Routed to the Renderer thread
    // at Highest priority so the GPU-side rebuild finishes before the game-side
    // remesh listener (GameLogic, High) runs.
    class RenderRegistryReloadListener : public Listener {
    public:
        // Must be public: EventManager's generated dispatch lambda calls the
        // kcOnEvent helper from outside the class.
        KC_EVENT_HANDLER(RegistryReloadEvent, onRegistryReload, EventPriority::Highest, ThreadName::Renderer)

    private:
        void onRegistryReload(RegistryReloadEvent& event);
    };

}