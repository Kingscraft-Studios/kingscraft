#include "Cache/Validators/ShaderCacheValidator.hpp"

#include "Util/LogUtils.hpp"
#include "Util/StringUtils.hpp"

namespace kc {
    bool ShaderCacheValidator::validate(const ShaderCacheHeader& header,
                                        const std::array<uint8_t, 16>& sourceHash) const {
        if (header.magic != SHADER_CACHE_MAGIC) {
            LogUtils::warn(ThreadName::Engine,
                "Shaders", StringUtils::build("Rejected: Invalid Magic. Expected: ", SHADER_CACHE_MAGIC));
            return false;
        }

        if (header.version != SHADER_CACHE_VERSION) {
            LogUtils::info(ThreadName::Engine,
                "Shaders", StringUtils::build("Rejected: Cache Version Mismatch. Expected: ", SHADER_CACHE_VERSION));
            return false;
        }

        if (header.sourceHash != sourceHash) {
            LogUtils::info(ThreadName::Engine, "Shaders", "Rejected: Source Changed");
            return false;
        }

        return true;
    }
}
