#pragma once
#include "IOTemplateBase.hpp"

#include "Util/LogUtils.hpp"
#include "Util/StringUtils.hpp"

#include <string>
#include <vector>

namespace kc {
    // Compiled shader blobs (SPIR-V). Raw bytes in, raw bytes out: the
    // template owns the missing-file policy and reports failures so the
    // bootstrapper never has to.
    class ShaderTemplate final : public IOTemplateBase {
    public:
        explicit ShaderTemplate(DiskLayer& disk) : IOTemplateBase(disk) {}

        std::vector<char> load(const std::string& path) {
            auto bytes = readBytes(path, MissingFile::Warn);
            if (!bytes || bytes->empty()) {
                LogUtils::error(ThreadName::Engine,
                    StringUtils::build("ShaderTemplate: Failed to load shader (", path, ")"));
                return {};
            }
            return std::move(*bytes);
        }

        // GLSL sources (relative to `root`) that shaderc can compile.
        std::vector<std::string> listSources(const std::string& root, bool recursive = true) const {
            std::vector<std::string> sources;
            for (const auto& rel : listFiles(root, recursive)) {
                if (rel.ends_with(".vert") || rel.ends_with(".frag") || rel.ends_with(".comp"))
                    sources.push_back(rel);
            }

            return sources;
        }
    };
}
