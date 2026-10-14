#pragma once
#include "IOTemplateBase.hpp"

#include "Util/LogUtils.hpp"

#include <string>
#include <vector>

namespace kc {
    // TrueType fonts handed straight to stb_truetype, which wants an
    // unsigned byte buffer. Reports load failures itself.
    class FontTemplate final : public IOTemplateBase {
    public:
        explicit FontTemplate(DiskLayer& disk) : IOTemplateBase(disk) {}

        std::vector<unsigned char> load(const std::string& path) {
            auto bytes = readBytes(path, MissingFile::Warn);
            if (!bytes || bytes->empty()) {
                LogUtils::error(ThreadName::Engine,
                    StringUtils::build("FontTemplate: Failed to load font (", path, ")"));
                return {};
            }
            return std::vector<unsigned char>(bytes->begin(), bytes->end());
        }
    };
}
