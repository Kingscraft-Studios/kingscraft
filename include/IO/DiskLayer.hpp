#pragma once
#include "IO/DiskOperations.hpp"

#include <cstddef>
#include <ios>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace kc {
    enum class MissingFile { Warn, Ignore };

    // The single owner of every on-disk access. All <filesystem>/<fstream>
    // usage in the engine lives behind this boundary: templates ask it for
    // bytes and it tells them nothing about how the disk works. Files are
    // cached per path for the session and guarded by a mutex so any thread
    // may call through a template.
    class DiskLayer {
    public:
        // Read
        std::optional<std::vector<char>> read(const std::string& path, MissingFile missing = MissingFile::Ignore);
        std::optional<std::vector<char>> readData(const std::string& path, std::size_t offset, std::size_t size, MissingFile missing = MissingFile::Ignore);
        bool readHeader(const std::string& path, void* header, std::size_t headerSize, MissingFile missing = MissingFile::Ignore);
        std::optional<std::size_t> size(const std::string& path, MissingFile missing = MissingFile::Ignore);

        // Write
        void write(const std::string& path, const std::vector<char>& data);
        void writeAt(const std::string& path, std::size_t offset, const std::vector<char>& data);
        void append(const std::string& path, const std::string& text);

        // Misc
        bool exists(const std::string& path) const;
        void createDirectories(const std::string& directory);
        // List Files under directory, if recursive is true it will output files under subdirectories too
        std::vector<std::string> list(const std::string& directory, bool recursive = true) const;

    private:
        DiskOperations& getFile(const std::string& path, std::ios::openmode mode);

        std::unordered_map<std::string, DiskOperations> files_;
        std::mutex filesMutex_;
    };
} // namespace kc
