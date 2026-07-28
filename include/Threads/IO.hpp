#pragma once
#include <memory>
#include <thread>
#include <string>
#include <vector>


namespace lve {
    class IO {
    public:

        static void Init();
        static void Shutdown();
        static IO& Get();

        void writeLogFile(const std::string& path, const std::string& text);
        void writeFile(const std::string& path, const std::vector<char>& data);
        std::vector<char> readFile(const std::string& path);

        bool readHeader(const std::string& path, void* header, size_t headerSize);
        std::vector<char> readData(const std::string& path, size_t offset, size_t size);

        size_t getFileSize(const std::string& path);
    private:

        static std::unique_ptr<IO> instance_;
    };
} // namespace lve
