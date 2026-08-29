#include "Threads/IO.hpp"

#include <filesystem>

#include <fstream>
#include <stdexcept>

namespace lve {

    std::unique_ptr<IO> IO::instance_ = nullptr;

    void IO::Init() {
        instance_ = std::make_unique<IO>();
    }

    void IO::Shutdown() {
        if (!instance_) return;
        instance_.reset();
    }

    IO& IO::Get() {
        return *instance_;
    }

    std::vector<char> IO::readFile(const std::string& path) {
        auto& file = getFile(path, std::ios::in | std::ios::out | std::ios::binary);

        return file.read();
    }

    void IO::writeFile(const std::string& path, const std::vector<char>& data) {
        getFile(path, std::ios::in | std::ios::out | std::ios::binary).writeTruncate(data);
    }

    void IO::writeFileAt(const std::string& path, std::size_t offset, const std::vector<char>& data) {
        getFile(path, std::ios::in | std::ios::out | std::ios::binary).writeAt(offset, data);
    }


    bool IO::readHeader(const std::string& path, void* header, size_t headerSize) {
        // don't create file on pure read - match old ifstream behavior (return false)
        if (!std::filesystem::exists(path))
            return false;
        try {
            auto& file = getFile(path, std::ios::in | std::ios::out | std::ios::binary);
            return file.readHeader(header, headerSize);
        } catch (const std::runtime_error&) {
            return false;
        }
    }

    std::vector<char> IO::readData(const std::string& path, size_t offset, size_t size) {
        if (!std::filesystem::exists(path))
            throw std::runtime_error("failed to open file: " + path);
        try {
            auto& file = getFile(path, std::ios::in | std::ios::out | std::ios::binary);
            return file.readData(offset, size);
        } catch (const std::runtime_error&) {
            throw std::runtime_error("failed to open file: " + path);
        }
    }

    size_t IO::getFileSize(const std::string& path) {
        if (!std::filesystem::exists(path))
            throw std::runtime_error("failed to open file: " + path);
        try {
            auto& file = getFile(path, std::ios::in | std::ios::out | std::ios::binary);
            return file.getFileSize();
        } catch (const std::runtime_error&) {
            throw std::runtime_error("failed to open file: " + path);
        }
    }

    // FIXME: Make a LogFile Which is same as DiskOperations
    void IO::writeLogFile(const std::string& path, const std::string& text) {

        // 1. ensure directory exists
        std::filesystem::path filePath(path);
        std::filesystem::create_directories(filePath.parent_path());

        // 2. write file
        std::ofstream file(path, std::ios::app);
        if (file.is_open()) {
            file << text << std::endl;
        }
    }

    DiskOperations& IO::getFile(const std::string& path, std::ios::openmode mode) {
        std::lock_guard<std::mutex> lock(filesMutex_);
        auto it = files_.find(path);

        if (it != files_.end()) {
            if (it->second.getMode() != mode) {
                throw std::runtime_error(
                    "File already opened with different mode: " + path
                );
            }

            return it->second;
        }

        auto [entry, inserted] =
            files_.try_emplace(path, path, mode);

        return entry->second;
    }
} // namespace lve
