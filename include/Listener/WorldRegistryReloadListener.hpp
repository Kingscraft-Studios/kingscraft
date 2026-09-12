#pragma once

#include "Event/Events/RegistryReloadPostEvent.hpp"
#include "Event/KCEventHandler.hpp"
#include "Event/Listener.hpp"
#include "Event/detail/EventRegistrar.hpp"

namespace kc {

    // Game-side listener: runs on the GameLogic thread after the renderer has
    // finished rebuilding block textures, and updates everything that depends
    // on the new registry (world chunk geometry + hotbar icons).
    class WorldRegistryReloadListener : public Listener {
    public:
        // Must be public: EventManager's generated dispatch lambda calls the
        // kcOnEvent helper from outside the class.
        KC_EVENT_HANDLER(RegistryReloadPostEvent, onRegistryReloadPost)

    private:
        void onRegistryReloadPost(RegistryReloadPostEvent& event);
    };

}