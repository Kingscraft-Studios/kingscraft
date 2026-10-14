#include "Vulkan/ShaderCompiler.hpp"

#include "shaderc/shaderc.h"
#include "shaderc/shaderc.hpp"

#include <cstring>

#include "Util/LogUtils.hpp"
#include "Util/StringUtils.hpp"

namespace kc {
    namespace {
        shaderc_shader_kind toShadercKind(ShaderStage stage) {
            switch (stage) {
                case ShaderStage::Vertex:   return shaderc_glsl_vertex_shader;
                case ShaderStage::Fragment: return shaderc_glsl_fragment_shader;
                case ShaderStage::Compute:  return shaderc_glsl_compute_shader;
            }
            return shaderc_glsl_infer_from_source;
        }
    }

    std::optional<ShaderStage> ShaderCompiler::stageFromExtension(const std::string& path) {
        if (path.ends_with(".vert")) return ShaderStage::Vertex;
        if (path.ends_with(".frag")) return ShaderStage::Fragment;
        if (path.ends_with(".comp")) return ShaderStage::Compute;
        return std::nullopt;
    }

    std::optional<std::vector<char>> ShaderCompiler::compile(const std::string& source,
                                                             ShaderStage stage,
                                                             const std::string& name,
                                                             const ShaderCompilerOptions& options) const {
        shaderc::Compiler compiler;
        shaderc::CompileOptions compileOptions;

        const uint32_t version = options.vulkanVersion != 0
                                     ? options.vulkanVersion
                                     : shaderc_env_version_vulkan_1_2;
        compileOptions.SetTargetEnvironment(shaderc_target_env_vulkan, version);
        compileOptions.SetOptimizationLevel(options.optimize
                                                ? shaderc_optimization_level_performance
                                                : shaderc_optimization_level_zero);

        const auto result = compiler.CompileGlslToSpv(source,
                                                      toShadercKind(stage),
                                                      name.c_str(),
                                                      options.entryPoint.c_str(),
                                                      compileOptions);

        if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
            LogUtils::error(ThreadName::Engine,
                StringUtils::build("[ShaderCompiler] Failed to compile ", name, ": ",
                                     result.GetErrorMessage()));
            return std::nullopt;
        }

        if (result.GetNumWarnings() > 0) {
            LogUtils::warn(ThreadName::Engine,
                StringUtils::build("[ShaderCompiler] ", name, " compiled with ",
                                     result.GetNumWarnings(), " warning(s)"));
        }

        const std::vector<uint32_t> spirv(result.cbegin(), result.cend());

        std::vector<char> bytes(spirv.size() * sizeof(uint32_t));
        std::memcpy(bytes.data(), spirv.data(), bytes.size());

        return bytes;
    }
}
