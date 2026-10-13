#include "IO/Templates/IOTemplateBase.hpp"

#include "Threads/IO.hpp"

namespace kc {
    IOTemplateBase::IOTemplateBase(IO& io) : io_(io) {}

    bool IOTemplateBase::readHeader(const std::string& path, void* header, std::size_t headerSize) {
        return io_.readHeader(path, header, headerSize);
    }

    std::optional<std::vector<char>> IOTemplateBase::readBytes(const std::string& path, MissingFile missing) {
        return io_.readFile(path, missing);
    }

    std::optional<std::vector<char>> IOTemplateBase::readBytes(const std::string& path, std::size_t offset, std::size_t size) {
        return io_.readData(path, offset, size);
    }

    void IOTemplateBase::writeBytes(const std::string& path, const std::vector<char>& data) {
        io_.writeFile(path, data);
    }

    void IOTemplateBase::writeBytes(const std::string& path, std::size_t offset, const std::vector<char>& data) {
        io_.writeFileAt(path, offset, data);
    }

    std::vector<std::string> IOTemplateBase::splitLines(const std::string& text) {
        std::vector<std::string> lines;
        size_t pos = 0;
        while (pos <= text.size()) {
            size_t eol = text.find('\n', pos);
            std::string line = (eol == std::string::npos) ? text.substr(pos) : text.substr(pos, eol - pos);
            if (!line.empty()) lines.push_back(line);
            if (eol == std::string::npos) break;
            pos = eol + 1;
        }
        return lines;
    }

    std::string IOTemplateBase::joinLines(const std::vector<std::string>& lines) {
        std::string out;
        for (const auto& line : lines) {
            if (!out.empty()) out += '\n';
            out += line;
        }
        return out;
    }

    bool IOTemplateBase::isDirective(const std::string& line) {
        return !line.empty() && line[0] == '#';
    }

    std::optional<std::vector<std::string>> IOTemplateBase::readLines(const std::string& path, MissingFile missing) {
        auto bytes = readBytes(path, missing);
        if (!bytes) return std::nullopt;
        return splitLines(std::string(bytes->begin(), bytes->end()));
    }

    void IOTemplateBase::writeLines(const std::string& path, const std::vector<std::string>& lines) {
        std::string text = joinLines(lines);
        writeBytes(path, std::vector<char>(text.begin(), text.end()));
    }
}
