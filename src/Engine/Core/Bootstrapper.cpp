#include "Core/Bootstrapper.hpp"

#include "Event/EventManager.hpp"
#include "Listener/ChunkLifecycleListener.hpp"
#include "Listener/PlayerBlockInteractionListener.hpp"
#include "Listener/PlayerLifecycleListener.hpp"
#include "Listener/ProfilingCaptureListener.hpp"
#include "Listener/RenderRegistryReloadListener.hpp"
#include "Listener/WorldRegistryReloadListener.hpp"
#include "Threads/Engine.hpp"

namespace kc {

    std::unique_ptr<Bootstrapper> Bootstrapper::instance_ = nullptr;

    void Bootstrapper::Init() {
        instance_ = std::make_unique<Bootstrapper>();
    }

    void Bootstrapper::Shutdown() {
        instance_.reset();
    }

    Bootstrapper& Bootstrapper::Get() {
        return *instance_;
    }

    void Bootstrapper::load() {
        // Shaders: discovered, validated against the on-disk cache and
        // (re)compiled with shaderc only when new or changed.
        Engine::Get().getCacheManager().getShaderCache().load();

        // Pipeline cache
        Engine::Get().getCacheManager().getPipelineCache().load();

        // Engine-wide event listeners ...
        EventManager::get().registerListener<RenderRegistryReloadListener>();
        EventManager::get().registerListener<WorldRegistryReloadListener>();
        EventManager::get().registerListener<PlayerBlockInteractionListener>();
        EventManager::get().registerListener<PlayerLifecycleListener>();
        EventManager::get().registerListener<ChunkLifecycleListener>();
        EventManager::get().registerListener<ProfilingCaptureListener>();
    }
} // namespace kc