#pragma once

#include "Event/Events/RegistryReloadEvent.hpp"
#include "Event/KCEventHandler.hpp"
#include "Event/Listener.hpp"
#include "Event/detail/EventRegistrar.hpp"

namespace kc {

    // Renderer-side listener: rebuilds the texture array, world renderer, and
    // UI block texture after a registry reload. The rebuild patches per-block
    // texture offsets that the mesher reads, so this listener finishes before
    // RegistryReloadPostEvent is fired and game-side systems remesh.
    class RenderRegistryReloadListener : public Listener {
    public:
        // Must be public: EventManager's generated dispatch lambda calls the
        // kcOnEvent helper from outside the class.
        KC_EVENT_HANDLER(RegistryReloadEvent, onRegistryReload)

    private:
        void onRegistryReload(RegistryReloadEvent& event);
    };

}