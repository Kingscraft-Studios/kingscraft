#pragma once
#include <fstream>
#include <ios>
#include <iosfwd>
#include <mutex>
#include <vector>

namespace kc {
    class DiskOperations {
    public:

        DiskOperations(const std::string& path, const std::ios::openmode mode);
        ~DiskOperations() = default;

        std::vector<char> read();
        // Every write reports success. A full disk or a failed stream must
        // never be mistaken for a completed save, so callers can keep their
        // in-memory data until the bytes are confirmed written.
        bool write(const std::vector<char>& data);                              // positional at current ppos
        bool writeAt(std::size_t offset, const std::vector<char>& data);        // positional at offset
        bool writeTruncate(const std::vector<char>& data);                      // replace entire file

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
        mutable std::mutex opMutex_;
    };
}
