#include "Cache/Storage/ShaderCacheStorage.hpp"

#include "Cache/Constants/ShaderCacheConstants.hpp"
#include "Threads/IO.hpp"
#include <stdexcept>
#include "Util/LogUtils.hpp"
#include "Util/Md5.hpp"
#include "Util/PathUtils.hpp"
#include "Util/StringUtils.hpp"

namespace kc {

    namespace {
        std::string makeKey(const std::string& name, ShaderStage stage) {
            return StringUtils::toLower(name) + "-" + shaderStageSuffix(stage);
        }

        std::string hexDigest(const std::array<uint8_t, 16>& d) {
            static const char* k = "0123456789abcdef";
            std::string s;
            s.reserve(32);
            for (uint8_t b : d) {
                s.push_back(k[b >> 4]);
                s.push_back(k[b & 0x0F]);
            }
            return s;
        }
    }

    void ShaderCacheStorage::load() {
        auto& shaderTemplate = IO::Get().getBuiltinTemplates().getShaderTemplate();

        const auto sources = shaderTemplate.listSources(SHADER_SOURCE_DIR, true);

        LogUtils::info(ThreadName::Engine,
            StringUtils::build("[ShaderCache] Discovering shaders in ", SHADER_SOURCE_DIR,
                                 " (", sources.size(), sources.size() == 1 ? " source)" : " sources)"));

        std::size_t compiled = 0;
        std::size_t cached = 0;
        std::size_t failed = 0;

        for (const auto& rel : sources) {
            const auto stage = ShaderCompiler::stageFromExtension(rel);
            if (!stage) {
                LogUtils::error(ThreadName::Engine,
                    StringUtils::build("[ShaderCache] Unsupported shader extension: ", rel));
                ++failed;
                continue;
            }

            const std::string name  = StringUtils::toLower(PathUtils::stem(PathUtils::basename(rel)));
            const std::string key   = makeKey(name, *stage);
            const std::string sourcePath = std::string(SHADER_SOURCE_DIR) + "/" + rel;
            const std::string cacheName  = std::string(SHADER_CACHE_DIR) + "/" + key + SHADER_CACHE_FILE_EXTENSION;

            if (shaders.contains(key)) {
                LogUtils::error(ThreadName::Engine,
                    StringUtils::build("[ShaderCache] Duplicate shader name '", key,
                                         "' (from ", rel, "); aborting startup"));
                throw std::runtime_error("ShaderCache: duplicate shader name: " + key);
            }

            auto bytes = shaderTemplate.load(sourcePath);
            if (bytes.empty()) {
                LogUtils::error(ThreadName::Engine,
                    StringUtils::build("[ShaderCache] Failed to read source ", sourcePath));
                ++failed;
                continue;
            }

            const std::string source(bytes.begin(), bytes.end());
            const auto hash = Md5::hash(source);

            std::vector<char> spv;
            if (loadCached(key, cacheName, hash, spv)) {
                LogUtils::info(ThreadName::Engine,
                    StringUtils::build("[ShaderCache] ", key, " up to date (cached, ",
                                         spv.size(), " bytes)"));
                shaders[key] = std::move(spv);
                ++cached;
                continue;
            }

            auto compiledSpv = compiler_.compile(source, *stage, rel);
            if (!compiledSpv) {
                ++failed;
                continue;
            }

            ShaderCacheHeader header{};
            header.magic = SHADER_CACHE_MAGIC;
            header.version = SHADER_CACHE_VERSION;
            header.sourceHash = hash;

            IO::Get().getBuiltinTemplates().getCacheTemplate().write(cacheName, header, *compiledSpv);

            LogUtils::info(ThreadName::Engine,
                StringUtils::build("[ShaderCache] Compiled ", key, " (", compiledSpv->size(),
                                     " bytes SPIR-V, hash ", hexDigest(hash), ")"));
            shaders[key] = std::move(*compiledSpv);
            ++compiled;
        }

        LogUtils::info(ThreadName::Engine,
            StringUtils::build("[ShaderCache] Ready: ", shaders.size(),
                                 shaders.size() == 1 ? " shader (" : " shaders (",
                                 compiled, " compiled, ", cached, " cached, ", failed, " failed)"));
    }

    bool ShaderCacheStorage::has(const std::string& name, ShaderStage stage) const {
        return shaders.find(makeKey(name, stage)) != shaders.end();
    }

    const std::vector<char>& ShaderCacheStorage::get(const std::string& name, ShaderStage stage) const {
        const std::string key = makeKey(name, stage);
        const auto it = shaders.find(key);
        if (it == shaders.end()) {
            LogUtils::error(ThreadName::Engine,
                StringUtils::build("[ShaderCache] Shader not loaded: ", key));
            throw std::runtime_error("ShaderCache: shader not loaded: " + key);
        }
        return it->second;
    }

    bool ShaderCacheStorage::loadCached(const std::string& key,
                                        const std::string& cacheName,
                                        const std::array<uint8_t, 16>& hash,
                                        std::vector<char>& out) {
        auto& cacheTemplate = IO::Get().getBuiltinTemplates().getCacheTemplate();

        ShaderCacheHeader header{};
        if (!cacheTemplate.readHeader(cacheName, header, MissingFile::Ignore))
            return false; // no cache yet -> compile

        if (!validator_.validate(header, hash)) {
            LogUtils::info(ThreadName::Engine,
                StringUtils::build("[ShaderCache] ", key, " stale; recompiling"));
            return false;
        }

        auto payload = cacheTemplate.readPayload(cacheName, sizeof(ShaderCacheHeader), MissingFile::Ignore);
        if (!payload || payload->empty()) {
            LogUtils::warn(ThreadName::Engine,
                StringUtils::build("[ShaderCache] ", cacheName, " payload missing/empty; recompiling"));
            return false;
        }

        out = std::move(*payload);
        return true;
    }
}
