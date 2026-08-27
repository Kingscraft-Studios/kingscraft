#pragma once

#include "IOTemplateBase.hpp"
#include "Renderer/RendererSettings.hpp"
#include "Util/StringBuilder.hpp"

namespace lve {
    class IO;

    class ChunkTemplate final : public IOTemplateBase {
    public:
        explicit ChunkTemplate(IO& io) : IOTemplateBase(io) {}

        std::vector<uint8_t> load(int gridX, int gridZ) {
            auto path = buildPath(gridX, gridZ);
            auto fileData = readBytes(path);

            // Deserialize
            auto formatted = deserialize(fileData);

            return formatted;
        }
        void save(int gridX, int gridZ, const std::vector<uint8_t>& data) {
            auto path = buildPath(gridX, gridZ);

            // Serialize the BlockIds
            auto formatted = serialize(data);

            // Save to Disk
            writeBytes(path, std::vector<char>(formatted.begin(), formatted.end()));
        }

    private:
        static constexpr char BLOCK_TO_CHAR[256] = {
            '.',  // 0 = AIR
            'G',  // 1 = GRASS
            'S',  // 2 = STONE
            'D',  // 3 = DIRT
        };

        static uint8_t charToBlock(char c) {
            switch (c) {
                case 'G': return 1;
                case 'S': return 2;
                case 'D': return 3;
                default:  return 0;
            }
        }

        std::string buildPath(int gridX, int gridZ) {
            return StringBuilder::build("world/chunks/", gridX, "_", gridZ, ".txt");
        }

        std::string serialize(const std::vector<uint8_t>& data) {
            auto& s = RendererSettings::get();
            int N = s.chunkSize;
            int h = s.worldHeight;

            std::string out;
            out.reserve(N * h + h * 2);

            for (int y = 0; y < h; ++y) {
                if (y > 0) out += '\n';

                for (int z = 0; z < N; ++z) {
                    for (int x = 0; x < N; ++x) {
                        size_t idx = static_cast<size_t>(y) * N * N + static_cast<size_t>(z) * N + x;
                        uint8_t id = (idx < data.size()) ? data[idx] : 0;
                        out += (id < 4) ? BLOCK_TO_CHAR[id] : '?';
                    }
                    out += '\n';
                }
            }

            return out;
        }
        std::vector<uint8_t> deserialize(std::vector<char>& data) {
            if (data.empty()) return {};
            auto& s = RendererSettings::get();
            int N = s.chunkSize;
            int h = s.worldHeight;

            std::vector<uint8_t> blockData(static_cast<size_t>(N) * h * N, 0);

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

                for (int x = 0; x < N && x < static_cast<int>(line.size()); ++x) {
                    size_t idx = static_cast<size_t>(y) * N * N + static_cast<size_t>(z) * N + x;
                    if (idx < blockData.size())
                        blockData[idx] = charToBlock(line[x]);
                }
                dataLine++;
            }

            return blockData;
        }
    };
}
