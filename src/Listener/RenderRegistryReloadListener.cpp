#include "Listener/RenderRegistryReloadListener.hpp"

#include "Core/Runtime.hpp"
#include "Threads/RenderThread.hpp"

namespace kc {

    void RenderRegistryReloadListener::onRegistryReload(RegistryReloadEvent&) {
        // Routed to the Renderer thread by the dispatcher: rebuild the texture
        // array and world renderer between frames, directly. No MessageBus hop
        // needed — this body already runs on the renderer.
        Runtime::get().renderThread->reloadTextures();
    }

}