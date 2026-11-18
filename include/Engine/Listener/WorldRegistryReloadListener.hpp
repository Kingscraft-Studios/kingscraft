#pragma once

#include "Event/Events/RegistryReloadEvent.hpp"
#include "Event/KCEventHandler.hpp"
#include "Event/Listener.hpp"
#include "Event/detail/EventRegistrar.hpp"

namespace kc {

    // Game-side listener: runs on the GameLogic thread at High priority (after
    // the renderer listener has rebuilt block textures at Highest) and updates
    // everything that depends on the new registry (world chunk geometry +
    // hotbar icons).
    class WorldRegistryReloadListener : public Listener {
    public:
        // Must be public: EventManager's generated dispatch lambda calls the
        // kcOnEvent helper from outside the class.
        KC_EVENT_HANDLER(RegistryReloadEvent, onRegistryReload, EventPriority::High, ThreadName::GameLogic)

    private:
        void onRegistryReload(RegistryReloadEvent& event);
    };

}