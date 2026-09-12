#include "Core/Bootstrapper.hpp"
#include "Threads/IO.hpp"

#include <stdexcept>
#include <algorithm>
#include <fstream>

#include "Core/Validators/PipelineCacheValidator.hpp"
#include "Event/EventManager.hpp"
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
        try {
            pipelineCacheData_ = loadAndValidatePC();
        } catch (std::runtime_error& e) {
            LogUtils::error(ThreadName::Renderer, StringBuilder::build("Error: ", e.what()));
        }

        // Engine-wide event listeners. Bukkit style: each listener observes the
        // events it cares about and does everything itself inside its handler;
        // nothing in the engine reaches out to wire systems together. Stays
        // registered until EventManager is unregistered/destroyed.
        EventManager::get().registerListener<RenderRegistryReloadListener>();
        EventManager::get().registerListener<WorldRegistryReloadListener>();
    }

    const std::vector<char>& Bootstrapper::getShader(const std::string& path) const {
        auto it = shaders_.find(path);
        if (it == shaders_.end()) {
            throw std::runtime_error("Preloader: shader not preloaded: " + path);
        }
        return it->second;
    }

    const std::vector<char>& Bootstrapper::getPipelineCacheData() const {
        return pipelineCacheData_;
    }

    void Bootstrapper::loadShaderFile(const std::string& path) {
        auto data = IO::Get().readFile(path);
        shaders_[path] = std::move(data);
    }

    std::vector<char> Bootstrapper::loadAndValidatePC() {

        if (!IO::Get().readHeader("pipeline_cache.bin", &header, sizeof(PipelineCacheHeader))) {
            LogUtils::info(ThreadName::Engine, "[PipelineCache] No pipeline cache found or failed to read header.");
            return {};
        }


        if (!validator.validate(header)) {
            return {};
        }


        size_t fileSize = IO::Get().getFileSize("pipeline_cache.bin");

        if (fileSize < sizeof(PipelineCacheHeader)) {
            LogUtils::error(ThreadName::Engine, "[PipelineCache] Cache file is smaller than the header size.");
            return {};
        }

        size_t blobSize = fileSize - sizeof(PipelineCacheHeader);


        auto cache = IO::Get().readData("pipeline_cache.bin", sizeof(PipelineCacheHeader), blobSize);

        if (cache.empty()) {
            LogUtils::error(ThreadName::Engine, "[PipelineCache] Failed to read pipeline cache data.");
            return {};
        }

        return cache;
    }
} // namespace kc
