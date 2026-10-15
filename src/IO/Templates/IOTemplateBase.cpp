#include "IO/Templates/IOTemplateBase.hpp"

namespace kc {
    IOTemplateBase::IOTemplateBase(DiskLayer& disk) : disk_(disk) {}

    bool IOTemplateBase::readHeader(const std::string& path, void* header, std::size_t headerSize, MissingFile missing) {
        return disk_.readHeader(path, header, headerSize, missing);
    }

    std::optional<std::vector<char>> IOTemplateBase::readBytes(const std::string& path, MissingFile missing) {
        return disk_.read(path, missing);
    }

    std::optional<std::vector<char>> IOTemplateBase::readBytes(const std::string& path, std::size_t offset, std::size_t size, MissingFile missing) {
        return disk_.readData(path, offset, size, missing);
    }

    std::size_t IOTemplateBase::fileSize(const std::string& path, MissingFile missing) {
        auto size = disk_.size(path, missing);
        return size.value_or(0);
    }

    std::vector<std::string> IOTemplateBase::listFiles(const std::string& directory, bool recursive) const {
        return disk_.list(directory, recursive);
    }

    void IOTemplateBase::writeBytes(const std::string& path, const std::vector<char>& data) {
        disk_.write(path, data);
    }

    void IOTemplateBase::writeBytes(const std::string& path, std::size_t offset, const std::vector<char>& data) {
        disk_.writeAt(path, offset, data);
    }

    void IOTemplateBase::appendText(const std::string& path, const std::string& text) {
        disk_.append(path, text);
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
