#include "Core/Bootstrapper.hpp"
#include "Threads/IO.hpp"

#include <stdexcept>
#include <algorithm>
#include <fstream>

#include "Event/EventManager.hpp"
#include "Listener/ChunkLifecycleListener.hpp"
#include "Listener/PlayerBlockInteractionListener.hpp"
#include "Listener/PlayerLifecycleListener.hpp"
#include "Listener/ProfilingCaptureListener.hpp"
#include "Listener/RenderRegistryReloadListener.hpp"
#include "Listener/WorldRegistryReloadListener.hpp"
#include "Util/LogUtils.hpp"

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
        // Shaders
        loadShaderFile("resources/shaders/terrain.vert.spv");
        loadShaderFile("resources/shaders/terrain.frag.spv");
        loadShaderFile("resources/shaders/transparent.frag.spv");
        loadShaderFile("resources/shaders/ui.vert.spv");
        loadShaderFile("resources/shaders/ui.frag.spv");
        loadShaderFile("resources/shaders/composite.vert.spv");
        loadShaderFile("resources/shaders/composite.frag.spv");
        loadShaderFile("resources/shaders/highlight.vert.spv");
        loadShaderFile("resources/shaders/highlight.frag.spv");
        loadShaderFile("resources/shaders/PostProcess/bloom/gaussblur.vert.spv");
        loadShaderFile("resources/shaders/PostProcess/bloom/gaussblur.frag.spv");
        loadShaderFile("resources/shaders/PostProcess/bloom/colorpass.vert.spv");
        loadShaderFile("resources/shaders/PostProcess/bloom/colorpass.frag.spv");

        // Pipeline cache
        pipelineCacheStorage_.load();

        // Engine-wide event listeners. Bukkit style: each listener observes the
        // events it cares about and does everything itself inside its handler;
        // nothing in the engine reaches out to wire systems together. Stays
        // registered until EventManager is unregistered/destroyed.
        EventManager::get().registerListener<RenderRegistryReloadListener>();
        EventManager::get().registerListener<WorldRegistryReloadListener>();
        EventManager::get().registerListener<PlayerBlockInteractionListener>();
        EventManager::get().registerListener<PlayerLifecycleListener>();
        EventManager::get().registerListener<ChunkLifecycleListener>();
        EventManager::get().registerListener<ProfilingCaptureListener>();
    }

    const std::vector<char>& Bootstrapper::getShader(const std::string& path) const {
        auto it = shaders_.find(path);
        if (it == shaders_.end()) {
            throw std::runtime_error("Preloader: shader not preloaded: " + path);
        }
        return it->second;
    }

    void Bootstrapper::loadShaderFile(const std::string& path) {
        auto data = IO::Get().readFile(path);
        shaders_[path] = data ? std::move(*data) : std::vector<char>{};
    }
} // namespace kc
