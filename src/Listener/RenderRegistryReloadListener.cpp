#include "Listener/RenderRegistryReloadListener.hpp"

#include "Bus/MessageBus.hpp"
#include "Core/Runtime.hpp"
#include "Threads/RenderThread.hpp"

namespace kc {

    void RenderRegistryReloadListener::onRegistryReload(RegistryReloadEvent&) {
        // Runs on the reload worker thread: hand the texture array / world
        // renderer rebuild to the renderer thread so it executes between frames.
        MessageBus::Get().send(ThreadName::Renderer, []() {
            Runtime::get().renderThread->reloadTextures();
        });
    }

}