#pragma once
#include <memory>
#include <mutex>
#include <thread>
#include <string>
#include <unordered_map>
#include <vector>

#include "IO/DiskOperations.hpp"
#include "IO/IOBuiltInTemplates.hpp"


namespace lve {
    class IO {
    public:

        static void Init();
        static void Shutdown();
        static IO& Get();

        void writeLogFile(const std::string& path, const std::string& text);
        void writeFile(const std::string& path, const std::vector<char>& data); // replace entire file (writeTruncate)
        void writeFileAt(const std::string& path, std::size_t offset, const std::vector<char>& data); // positional write
        std::vector<char> readFile(const std::string& path);

        bool readHeader(const std::string& path, void* header, size_t headerSize);
        std::vector<char> readData(const std::string& path, size_t offset, size_t size);

        size_t getFileSize(const std::string& path);

        void flushTemplates();   // write back staged template data (e.g. region batches)


        IOBuiltInTemplates& getBuiltinTemplates() { return builtInTemplates; }
    private:
        DiskOperations& getFile(const std::string& path, std::ios::openmode mode);

        IOBuiltInTemplates builtInTemplates{*this};
        std::unordered_map<std::string, DiskOperations> files_;
        std::mutex filesMutex_;
        static std::unique_ptr<IO> instance_;
    };
} // namespace lve
