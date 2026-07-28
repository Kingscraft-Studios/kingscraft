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
        std::ifstream file{path, std::ios::ate | std::ios::binary};

        if (!file.is_open()) {
            throw std::runtime_error("failed to open file: " + path);
        }

        size_t fileSize = static_cast<size_t>(file.tellg());
        std::vector<char> buffer(fileSize);

        file.seekg(0);
        file.read(buffer.data(), fileSize);

        file.close();
        return buffer;
    }

    void IO::writeFile(const std::string& path, const std::vector<char>& data) {
        auto parent = std::filesystem::path(path).parent_path();
        if (!parent.empty())
            std::filesystem::create_directories(parent);

        std::ofstream file(path, std::ios::binary);
        if (file.is_open())
            file.write(data.data(), data.size());
    }

    // FIXME: Accessible Through MessageBus
    bool IO::readHeader(const std::string& path, void* header, size_t headerSize) {
        std::ifstream file(path, std::ios::binary);

        if (!file.is_open())
            return false;

        file.read(static_cast<char*>(header), headerSize);

        return file.good();
    }

    std::vector<char> IO::readData(const std::string& path, size_t offset, size_t size) {
        std::ifstream file(path, std::ios::binary);

        if (!file.is_open())
            throw std::runtime_error("failed to open file: " + path);

        file.seekg(offset);

        std::vector<char> buffer(size);

        file.read(buffer.data(), static_cast<std::streamsize>(size));

        return buffer;
    }

    size_t IO::getFileSize(const std::string& path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);

        if (!file.is_open())
            throw std::runtime_error("failed to open file: " + path);

        return file.tellg();
    }

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

} // namespace lve
