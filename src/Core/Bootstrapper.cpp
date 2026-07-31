#include "Core/Bootstrapper.hpp"
#include "Threads/IO.hpp"

#include <stdexcept>
#include <algorithm>
#include <fstream>

#include "Core/Validators/PipelineCacheValidator.hpp"
#include "Util/LogUtils.hpp"

namespace lve {

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

void Bootstrapper::loadAll() {
    // Shaders
    loadShaderFile("resources/shaders/terrain.vert.spv");
    loadShaderFile("resources/shaders/terrain.frag.spv");
    loadShaderFile("resources/shaders/ui.vert.spv");
    loadShaderFile("resources/shaders/ui.frag.spv");
    loadShaderFile("resources/shaders/composite.vert.spv");
    loadShaderFile("resources/shaders/composite.frag.spv");
    loadShaderFile("resources/shaders/PostProcess/bloom/gaussblur.vert.spv");
    loadShaderFile("resources/shaders/PostProcess/bloom/gaussblur.frag.spv");
    loadShaderFile("resources/shaders/PostProcess/bloom/colorpass.vert.spv");
    loadShaderFile("resources/shaders/PostProcess/bloom/colorpass.frag.spv");

    // Pipeline cache
    try {
        pipelineCacheData_ = loadAndValidatePipelineCache();
    } catch (std::runtime_error& e) {
        LogUtils::error(ThreadName::Renderer, StringBuilder::build("Error: ", e.what()));
    }
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

    std::vector<char> Bootstrapper::loadAndValidatePipelineCache() {
    // TODO: add Logging

    if (!IO::Get().readHeader(
            "pipeline_cache.bin",
            &header,
            sizeof(PipelineCacheHeader)))
    {
        return {};
    }


    if (!validator.validate(header))
    {
        return {};
    }


    size_t fileSize = IO::Get().getFileSize("pipeline_cache.bin");

    if (fileSize < sizeof(PipelineCacheHeader))

    {
        return {};
    }

    size_t blobSize =
        fileSize - sizeof(PipelineCacheHeader);


    return IO::Get().readData(
        "pipeline_cache.bin",
        sizeof(PipelineCacheHeader),
        blobSize
    );
}
} // namespace lve
