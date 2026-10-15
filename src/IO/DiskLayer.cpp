#include "IO/DiskLayer.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "Util/LogUtils.hpp"

namespace kc {

    bool DiskLayer::exists(const std::string& path) const {
        return std::filesystem::exists(path);
    }

    void DiskLayer::createDirectories(const std::string& directory) {
        if (directory.empty()) return;
        std::filesystem::create_directories(directory);
    }

    std::optional<std::vector<char>> DiskLayer::read(const std::string& path, MissingFile missing) {
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

    bool DiskLayer::write(const std::string& path, const std::vector<char>& data) {
        return getFile(path, std::ios::in | std::ios::out | std::ios::binary).writeTruncate(data);
    }

    bool DiskLayer::writeAt(const std::string& path, std::size_t offset, const std::vector<char>& data) {
        return getFile(path, std::ios::in | std::ios::out | std::ios::binary).writeAt(offset, data);
    }

    bool DiskLayer::readHeader(const std::string& path, void* header, size_t headerSize, MissingFile missing) {
        // don't create file on pure read - match old ifstream behavior (return false)
        if (!std::filesystem::exists(path)) {
            if (missing == MissingFile::Warn)
                LogUtils::warn(ThreadName::Engine,
                    "Unable to Read File: File Doesn't Exist (" + path + ")");
            return false;
        }

        auto& file = getFile(path, std::ios::in | std::ios::out | std::ios::binary);

        return file.readHeader(header, headerSize);
    }

    std::optional<std::vector<char>> DiskLayer::readData(const std::string& path, size_t offset, size_t size, MissingFile missing) {
        if (!std::filesystem::exists(path)) {
            if (missing == MissingFile::Warn)
                LogUtils::warn(ThreadName::Engine,
                    "Unable to Read File: File Doesn't Exist (" + path + ")");

            return std::nullopt;
        }

        auto& file = getFile(path, std::ios::in | std::ios::out | std::ios::binary);

        return file.readData(offset, size);
    }

    std::optional<size_t> DiskLayer::size(const std::string& path, MissingFile missing) {
        if (!std::filesystem::exists(path)) {
            if (missing == MissingFile::Warn)
                LogUtils::warn(ThreadName::Engine,
                    "Unable to Read File: File Doesn't Exist (" + path + ")");
            return std::nullopt;
        }

        auto& file = getFile(path, std::ios::in | std::ios::out | std::ios::binary);

        return file.getFileSize();
    }

    void DiskLayer::append(const std::string& path, const std::string& text) {
        // Deliberately bypasses DiskOperations/LogUtils: logging is the lowest
        // layer and must never route a failure back into itself.
        std::filesystem::path filePath(path);
        createDirectories(filePath.parent_path().string());

        std::ofstream file(path, std::ios::app);
        if (file.is_open()) {
            file << text << std::endl;
        }
    }

    std::vector<std::string> DiskLayer::list(const std::string& directory, bool recursive) const {
        std::vector<std::string> out;

        std::error_code ec;
        if (!std::filesystem::is_directory(directory, ec))
            return out;

        const auto opts = std::filesystem::directory_options::skip_permission_denied;
        if (recursive) {
            for (std::filesystem::recursive_directory_iterator it(directory, opts, ec), end; it != end; it.increment(ec)) {
                if (ec) break;
                if (!it->is_regular_file(ec)) continue;
                out.push_back(it->path().lexically_relative(directory).generic_string());
            }
        } else {
            for (std::filesystem::directory_iterator it(directory, opts, ec), end; it != end; it.increment(ec)) {
                if (ec) break;
                if (!it->is_regular_file(ec)) continue;
                out.push_back(it->path().filename().generic_string());
            }
        }

        std::sort(out.begin(), out.end());
        return out;
    }

    DiskOperations& DiskLayer::getFile(const std::string& path, std::ios::openmode mode) {
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
