#pragma once

#include "IO/DiskLayer.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace kc {
    // Every concrete template derives from this and exposes only the domain
    // methods its subsystem needs (load/save/flush...). It owns the disk layer
    // so the rest of the engine never sees a path, a stream or a MissingFile
    // policy. MissingFile defaults to the quiet Ignore; templates that must
    // surface a missing file (cache, shaders, models, textures) pass Warn.
    class IOTemplateBase {
    protected:
        explicit IOTemplateBase(DiskLayer& disk);

        // Read
        std::optional<std::vector<char>> readBytes(const std::string& path, MissingFile missing = MissingFile::Ignore);
        std::optional<std::vector<char>> readBytes(const std::string& path, std::size_t offset, std::size_t size, MissingFile missing = MissingFile::Ignore);
        std::optional<std::vector<std::string>> readLines(const std::string& path, MissingFile missing = MissingFile::Ignore);
        bool readHeader(const std::string& path, void* header, std::size_t headerSize, MissingFile missing = MissingFile::Ignore);
        std::vector<std::string> listFiles(const std::string& directory, bool recursive = true) const;

        template <typename T>
        bool readHeader(const std::string& path, T& value, MissingFile missing = MissingFile::Ignore) {
            static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
            return readHeader(path, &value, sizeof(T), missing);
        }

        // Write
        void writeBytes(const std::string& path, const std::vector<char>& data);
        void writeBytes(const std::string& path, std::size_t offset, const std::vector<char>& data);
        void writeLines(const std::string& path, const std::vector<std::string>& lines);
        void appendText(const std::string& path, const std::string& text);

        static std::vector<std::string> splitLines(const std::string& text);
        static std::string joinLines(const std::vector<std::string>& lines);
        static bool isDirective(const std::string& line);

    private:
        DiskLayer& disk_;
    };
}
