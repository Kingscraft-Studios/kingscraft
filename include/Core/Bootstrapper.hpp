#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

#include "Cache/Storage/PipelineCacheStorage.hpp"

namespace kc {

class Bootstrapper {
public:
    static void Init();
    static void Shutdown();
    static Bootstrapper& Get();

    void load();

    const std::vector<char>& getShader(const std::string& path) const;

    PipelineCacheStorage& getPipelineCacheStorage() { return pipelineCacheStorage_; }

private:

    void loadShaderFile(const std::string& path);

    PipelineCacheStorage pipelineCacheStorage_;
    std::unordered_map<std::string, std::vector<char>> shaders_;

    static std::unique_ptr<Bootstrapper> instance_;
};

} // namespace kc
