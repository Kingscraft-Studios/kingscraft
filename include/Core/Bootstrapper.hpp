#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

#include "Validators/PipelineCacheValidator.hpp"

namespace kc {

class Bootstrapper {
public:
    static void Init();
    static void Shutdown();
    static Bootstrapper& Get();

    void load();

    const std::vector<char>& getShader(const std::string& path) const;
    const std::vector<char>& getPipelineCacheData() const;

    PipelineCacheValidator& getPipelineValidator() { return validator; }
    PipelineCacheHeader getPipelineCacheHeader() { return header; }

private:

    void loadShaderFile(const std::string& path);
    std::vector<char> loadAndValidatePC();

    PipelineCacheValidator validator;
    std::unordered_map<std::string, std::vector<char>> shaders_;
    PipelineCacheHeader header{};
    std::vector<char> pipelineCacheData_;

    static std::unique_ptr<Bootstrapper> instance_;
};

} // namespace kc
