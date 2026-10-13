#pragma once

#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace kc {
    class IO;

    enum class MissingFile { Warn, Ignore };

    class IOTemplateBase {
    protected:
        explicit IOTemplateBase(IO& io);

        // --- raw byte I/O ---
        std::optional<std::vector<char>> readBytes(const std::string& path, MissingFile missing = MissingFile::Warn);
        std::optional<std::vector<char>> readBytes(const std::string& path, std::size_t offset, std::size_t size);
        void writeBytes(const std::string& path, const std::vector<char>& data);
        void writeBytes(const std::string& path, std::size_t offset, const std::vector<char>& data);

        // --- typed POD header read (byte-exact, fails cleanly on short files) ---
        bool readHeader(const std::string& path, void* header, std::size_t headerSize);

        template <typename T>
        bool readHeader(const std::string& path, T& value) {
            static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
            return readHeader(path, &value, sizeof(T));
        }

        // --- shared text/directive helpers (Region + WorldMetadata) ---
        static std::vector<std::string> splitLines(const std::string& text);
        static std::string joinLines(const std::vector<std::string>& lines);
        static bool isDirective(const std::string& line);

        std::optional<std::vector<std::string>> readLines(const std::string& path, MissingFile missing = MissingFile::Warn);
        void writeLines(const std::string& path, const std::vector<std::string>& lines);

    private:
        IO& io_;
    };
}
