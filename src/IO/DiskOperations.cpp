#include "IO/DiskOperations.hpp"

#include <filesystem>

namespace lve {
    DiskOperations::DiskOperations(const std::string& path, const std::ios::openmode mode) : path_(path), mode_(mode), file(openFile(path, mode_)) {
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open File: " + path);
        }
    }

    size_t DiskOperations::getFileSize() {
        file.clear();

        const auto current = file.tellg();
        file.seekg(0, std::ios::end);

        const auto size = static_cast<std::size_t>(file.tellg());
        file.seekg(current);

        return size;
    }

    bool DiskOperations::readHeader(void* header, size_t headerSize) {
        file.clear();
        file.seekg(0);

        file.read(static_cast<char*>(header), static_cast<std::streamsize>(headerSize));

        return static_cast<bool>(file);
    }

    std::vector<char> DiskOperations::read() {
        file.clear();

        file.seekg(0, std::ios::end);
        const auto fileSize = static_cast<std::size_t>(file.tellg());

        file.seekg(0);

        std::vector<char> buffer(fileSize);

        file.read(
            buffer.data(),
            static_cast<std::streamsize>(fileSize)
        );

        return buffer;
    }

    std::vector<char> DiskOperations::readData(size_t offset) {
        file.clear();
        file.seekg(0, std::ios::end);

        const auto fileSize = static_cast<std::size_t>(file.tellg());

        if (offset > fileSize)
            return {};

        const auto dataSize = fileSize - offset;

        file.seekg(static_cast<std::streamoff>(offset));

        std::vector<char> buffer(dataSize);

        file.read(
            buffer.data(),
            static_cast<std::streamsize>(dataSize)
        );

        buffer.resize(static_cast<size_t>(file.gcount()));

        return buffer;
    }

    std::vector<char> DiskOperations::readData(size_t offset, size_t size) {
        file.clear();
        file.seekg(0, std::ios::end);

        const auto fileSize = static_cast<std::size_t>(file.tellg());

        if (offset > fileSize)
            return {};

        // clamp to EOF like IO::readData previously did (gcount may be < size)
        const auto remaining = fileSize - offset;
        const auto toRead = std::min(size, remaining);

        file.seekg(static_cast<std::streamoff>(offset));

        std::vector<char> buffer(size);

        if (toRead > 0) {
            file.read(buffer.data(), static_cast<std::streamsize>(toRead));
            // keep IO semantics: return buffer of requested size (zero-padded tail if short)
            // but resize to actually read if you prefer strict; we keep size and let caller use gcount semantics
            // To exactly mimic old IO::readData which returned size-sized buffer even on short read,
            // we keep buffer size == requested size and leave remainder zero-initialized
            // file.gcount() tells how many valid bytes were read
            const size_t got = static_cast<size_t>(file.gcount());
            if (got < size) {
                // zero out unread tail already zero-initialized by vector(size)
                // optionally shrink: buffer.resize(got); but IO expected size -> keep size
                // we keep original size to preserve caller expectation (caller can still check)
                // If strict EOF truncation desired, uncomment next line:
                // buffer.resize(got);
                (void)got;
            }
        } else {
            // offset at EOF, no bytes to read - keep zero-initialized buffer of requested size per old behavior
        }

        return buffer;
    }

    void DiskOperations::write(const std::vector<char>& data) {
        // positional write at current put position - does not truncate
        file.write(data.data(), static_cast<std::streamsize>(data.size()));
    }

    void DiskOperations::writeAt(std::size_t offset, const std::vector<char>& data) {
        file.clear();
        file.seekp(static_cast<std::streamoff>(offset));
        file.write(data.data(), static_cast<std::streamsize>(data.size()));
        file.flush();
    }

    void DiskOperations::writeTruncate(const std::vector<char>& data) {
        file.clear();
        file.close();

        file.open(path_, mode_ | std::ios::trunc);

        if (!file.is_open())
            throw std::runtime_error("Failed to reopen file: " + path_);

        file.write(data.data(), static_cast<std::streamsize>(data.size()));

        if (!file)
            throw std::runtime_error("Failed to write file: " + path_);

        file.flush();
        file.close();

        // Restore the original mode.
        file.open(path_, mode_);

        if (!file.is_open())
            throw std::runtime_error("Failed to reopen file: " + path_);
    }

    // Helper Methods

    std::fstream DiskOperations::openFile(const std::string& path, std::ios::openmode mode) {
        const auto parent = std::filesystem::path(path).parent_path();

        if (!parent.empty())
            std::filesystem::create_directories(parent);

        std::fstream file{path, mode};

        if (!file.is_open()) {
            // in|out alone doesn't create a nonexistent file - create it only if out is requested
            if (!(mode & std::ios::out))
                return file;

            std::ofstream create{path, std::ios::binary};

            if (!create.is_open())
                return file;

            create.close();

            file.open(path, mode);
        }

        return file;
    }
}
