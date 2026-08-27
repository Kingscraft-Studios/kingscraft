#include "IO/Templates/IOTemplateBase.hpp"

#include "Threads/IO.hpp"

namespace lve {
    IOTemplateBase::IOTemplateBase(IO& io) : io_(io) {}

    std::vector<char> IOTemplateBase::readBytes(const std::string& path) {
        return io_.readFile(path);
    }

    std::vector<char> IOTemplateBase::readBytes(const std::string& path, std::size_t offset, std::size_t size) {
        return io_.readData(path, offset, size);
    }

    void IOTemplateBase::writeBytes(const std::string& path, const std::vector<char>& data) {
        io_.writeFile(path, data);
    }

    void IOTemplateBase::writeBytes(const std::string& path, std::size_t offset, const std::vector<char>& data) {
        io_.writeFileAt(path, offset, data);
    }
}
