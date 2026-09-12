#pragma once

#include "IOTemplateBase.hpp"
#include "Renderer/RendererSettings.hpp"
#include "Util/StringBuilder.hpp"

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace kc {
    class IO;

    class ChunkTemplate final : public IOTemplateBase {
    public:
        explicit ChunkTemplate(IO& io) : IOTemplateBase(io) {}

        std::vector<uint64_t> load(int gridX, int gridZ) {
            auto path = buildPath(gridX, gridZ);
            auto fileData = readBytes(path);

            // Deserialize
            auto formatted = deserialize(fileData);

            return formatted;
        }
        void save(int gridX, int gridZ, const std::vector<uint64_t>& data) {
            auto path = buildPath(gridX, gridZ);

            // Serialize the BlockIds
            auto formatted = serialize(data);

            // Save to Disk
            writeBytes(path, std::vector<char>(formatted.begin(), formatted.end()));
        }

        // Pure encode/decode (no file I/O) — shared with RegionTemplate so
        // region files reuse the same block-grid text layout.
        //
        // Each cell is the encoded uint64 identifier as 16 lowercase hex digits
        // (0 = air/empty), space separated, one row per line. Rows are grouped
        // per horizontal slice with a blank line between slices.
        std::string serialize(const std::vector<uint64_t>& data) {
            auto& s = RendererSettings::get();
            int N = s.chunkSize;
            int h = s.worldHeight;

            static const char* HEX = "0123456789abcdef";

            std::string out;
            out.reserve(static_cast<size_t>(N) * h * (16 + 1));

            auto appendHex = [&out](uint64_t v) {
                char buf[16];
                for (int i = 15; i >= 0; --i) { buf[i] = HEX[v & 0xF]; v >>= 4; }
                out.append(buf, 16);
            };

            for (int y = 0; y < h; ++y) {
                if (y > 0) out += '\n';

                for (int z = 0; z < N; ++z) {
                    for (int x = 0; x < N; ++x) {
                        size_t idx = static_cast<size_t>(y) * N * N + static_cast<size_t>(z) * N + x;
                        uint64_t id = (idx < data.size()) ? data[idx] : 0;
                        appendHex(id);
                        out += ' ';
                    }
                    out += '\n';
                }
            }

            return out;
        }
        std::vector<uint64_t> deserialize(std::vector<char>& data) {
            if (data.empty()) return {};
            auto& s = RendererSettings::get();
            int N = s.chunkSize;
            int h = s.worldHeight;

            std::vector<uint64_t> blockData(static_cast<size_t>(N) * h * N, 0);

            std::string content(data.begin(), data.end());
            size_t pos = 0;
            int dataLine = 0;

            while (pos < content.size() && dataLine < h * N) {
                size_t eol = content.find('\n', pos);
                if (eol == std::string::npos) eol = content.size();

                std::string line = content.substr(pos, eol - pos);
                pos = eol + 1;

                if (line.empty() || line[0] == '#') continue;

                int y = dataLine / N;
                int z = dataLine % N;

                std::istringstream iss(line);
                for (int x = 0; x < N; ++x) {
                    uint64_t id = 0;
                    if (!(iss >> std::hex >> id)) break;
                    size_t idx = static_cast<size_t>(y) * N * N + static_cast<size_t>(z) * N + x;
                    if (idx < blockData.size())
                        blockData[idx] = id;
                }
                dataLine++;
            }

            return blockData;
        }

    private:
        std::string buildPath(int gridX, int gridZ) {
            return StringBuilder::build("world/chunks/", gridX, "_", gridZ, ".txt");
        }
    };
}