#pragma once
#include <string>
#include <vector>

namespace lve {
    class IO;

    class IOTemplateBase {
    protected:
        explicit IOTemplateBase(IO& io);

        /*Read Whole File*/
        std::vector<char> readBytes(const std::string& path);


        std::vector<char> readBytes(const std::string& path, std::size_t offset, std::size_t size);


        void writeBytes(const std::string& path, const std::vector<char>& data);

        /*Write at specific offset*/
        void writeBytes(const std::string& path, std::size_t offset, const std::vector<char>& data);

    private:
        IO& io_;
    };
}
