#pragma once
#include <optional>
#include <string>
#include <vector>

namespace kc {
    class IO;

    // Read policy for whole-file reads:
    //   Warn   - a missing file is unexpected; log a warning and return nullopt
    //   Ignore - a missing file is normal (new world / first region access);
    //            stay silent and return nullopt
    enum class MissingFile { Warn, Ignore };

    class IOTemplateBase {
    protected:
        explicit IOTemplateBase(IO& io);

        /*Read Whole File*/
        std::optional<std::vector<char>> readBytes(const std::string& path, MissingFile missing = MissingFile::Warn);


        std::optional<std::vector<char>> readBytes(const std::string& path, std::size_t offset, std::size_t size);


        void writeBytes(const std::string& path, const std::vector<char>& data);

        /*Write at specific offset*/
        void writeBytes(const std::string& path, std::size_t offset, const std::vector<char>& data);

    private:
        IO& io_;
    };
}
