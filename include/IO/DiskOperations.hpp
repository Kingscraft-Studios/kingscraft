#pragma once
#include <fstream>
#include <ios>
#include <iosfwd>
#include <vector>

namespace lve {
    class DiskOperations {
    public:

        DiskOperations(const std::string& path, const std::ios::openmode mode);
        ~DiskOperations() = default;

        std::vector<char> read();
        void write(const std::vector<char>& data);                              // positional at current ppos
        void writeAt(std::size_t offset, const std::vector<char>& data);        // positional at offset
        void writeTruncate(const std::vector<char>& data);                      // replace entire file

        std::vector<char> readData(size_t offset);
        std::vector<char> readData(size_t offset, size_t size);
        size_t getFileSize();

        bool readHeader(void* header, size_t headerSize);

        std::ios::openmode getMode() const { return mode_; }

        /* Generic Read Header */
        template<typename T>
        bool readHeader(T& value) {
            static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
            return readHeader(&value, sizeof(T));
        }
    private:
        static std::fstream openFile(const std::string& path, std::ios::openmode mode);

        const std::string path_;
        const std::ios::openmode mode_;
        std::fstream file;
    };
}
