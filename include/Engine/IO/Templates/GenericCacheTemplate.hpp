#pragma once
#include "IOTemplateBase.hpp"

#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace kc {

    class GenericCacheTemplate final : public IOTemplateBase {
    public:
        explicit GenericCacheTemplate(DiskLayer& disk) : IOTemplateBase(disk) {}

        // Typed header read against the cache directory.
        template <typename Header>
        bool readHeader(const std::string& filename, Header& header, MissingFile missing = MissingFile::Warn) {
            static_assert(std::is_trivially_copyable_v<Header>, "Header must be trivially copyable");
            return IOTemplateBase::readHeader(path(filename), header, missing);
        }

        // Payload that follows a headerSize-byte header. Missing/short -> nullopt.
        std::optional<std::vector<char>> readPayload(const std::string& filename, std::size_t headerSize, MissingFile missing = MissingFile::Warn) {
            auto bytes = readBytes(path(filename), missing);
            if (!bytes || bytes->size() < headerSize) return std::nullopt;
            return std::vector(bytes->begin() + headerSize, bytes->end());
        }

        // Replace the whole cache file: header immediately followed by payload.
        void write(const std::string& filename, const void* header, std::size_t headerSize, const std::vector<char>& payload) {
            std::vector<char> out(headerSize + payload.size());
            if (headerSize)       std::memcpy(out.data(), header, headerSize);
            if (!payload.empty()) std::memcpy(out.data() + headerSize, payload.data(), payload.size());
            writeBytes(path(filename), out);
        }

        template <typename Header>
        void write(const std::string& filename, const Header& header, const std::vector<char>& payload) {
            static_assert(std::is_trivially_copyable_v<Header>, "Header must be trivially copyable");
            write(filename, &header, sizeof(Header), payload);
        }

    private:
        static std::string path(const std::string& filename) {
            return std::string(DIRECTORY) + "/" + filename;
        }

        static constexpr std::string_view DIRECTORY = "cache";
    };
}
