#include "Threads/IO.hpp"

#include <filesystem>

#include <fstream>
#include <stdexcept>

#include "Util/LogUtils.hpp"

namespace kc {

    std::unique_ptr<IO> IO::instance_ = nullptr;

    void IO::Init() {
        instance_ = std::make_unique<IO>();
    }

    void IO::Shutdown() {
        if (!instance_) return;
        instance_->flushTemplates();
        instance_.reset();
    }

    void IO::flushTemplates() {
        builtInTemplates.getRegionTemplate().flush();
        builtInTemplates.getWorldMetadataTemplate().flush();
    }

    IO& IO::Get() {
        return *instance_;
    }

    bool IO::exists(const std::string& path) const {
        return std::filesystem::exists(path);
    }

    void IO::createDirectories(const std::string& directory) {
        if (directory.empty()) return;
        std::filesystem::create_directories(directory);
    }

    std::optional<std::vector<char>> IO::readFile(const std::string& path, MissingFile missing) {
        // A read must never create anything on disk (no directory, no empty file).
        if (!std::filesystem::exists(path)) {
            if (missing == MissingFile::Warn)
                LogUtils::warn(ThreadName::Engine,
                    "Unable to Read File: File Doesn't Exist (" + path + ")");
            return std::nullopt;
        }

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

    std::optional<std::vector<char>> IO::readData(const std::string& path, size_t offset, size_t size) {
        if (!std::filesystem::exists(path)) {
            LogUtils::warn(ThreadName::Engine,
                "Unable to Read File: File Doesn't Exist (" + path + ")");
            return std::nullopt;
        }
        try {
            auto& file = getFile(path, std::ios::in | std::ios::out | std::ios::binary);
            return file.readData(offset, size);
        } catch (const std::runtime_error&) {
            LogUtils::warn(ThreadName::Engine,
                "Unable to Read File: File Doesn't Exist (" + path + ")");
            return std::nullopt;
        }
    }

    std::optional<size_t> IO::getFileSize(const std::string& path) {
        if (!std::filesystem::exists(path)) {
            LogUtils::warn(ThreadName::Engine,
                "Unable to Read File: File Doesn't Exist (" + path + ")");
            return std::nullopt;
        }
        try {
            auto& file = getFile(path, std::ios::in | std::ios::out | std::ios::binary);
            return file.getFileSize();
        } catch (const std::runtime_error&) {
            LogUtils::warn(ThreadName::Engine,
                "Unable to Read File: File Doesn't Exist (" + path + ")");
            return std::nullopt;
        }
    }

    // FIXME: Make a LogFile Which is same as DiskOperations
    void IO::writeLogFile(const std::string& path, const std::string& text) {

        // 1. ensure directory exists
        std::filesystem::path filePath(path);
        createDirectories(filePath.parent_path().string());

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
} // namespace kc
