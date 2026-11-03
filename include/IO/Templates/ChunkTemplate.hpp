#pragma once

#include "IOTemplateBase.hpp"
#include "Renderer/RendererSettings.hpp"
#include "Core/World/PackedPos.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include "Util/StringUtils.hpp"

namespace kc {
    class DiskLayer;

    class ChunkTemplate final : public IOTemplateBase {
    public:
        explicit ChunkTemplate(DiskLayer& disk) : IOTemplateBase(disk) {}

        std::vector<uint64_t> load(int gridX, int gridZ) {
            auto path = buildPath(gridX, gridZ);
            auto fileData = readBytes(path);
            if (!fileData) return {};

            // Deserialize
            auto formatted = deserialize(*fileData);

            return formatted;
        }
        void save(int gridX, int gridZ, const std::vector<uint64_t>& data) {
            auto path = buildPath(gridX, gridZ);

            // Serialize the per-cell block ids
            auto formatted = serialize(data);

            // Save to Disk
            writeBytes(path, std::vector<char>(formatted.begin(), formatted.end()));
        }

        static constexpr uint32_t BINARY_SPARSE_MAGIC = 0x53424150;
        static constexpr uint16_t BINARY_SPARSE_VERSION = 2;

        std::string serialize(const std::vector<uint64_t>& data) {
            auto& s = RendererSettings::get();
            int N = s.chunkSize;
            int h = s.worldHeight;

            std::vector<char> out;
            const size_t total = static_cast<size_t>(N) * h * N;
            out.reserve(4 + 2 + 4 + total * 12);

            auto writeU32 = [&](uint32_t v) {
                out.push_back(v & 0xFF);
                out.push_back((v >> 8) & 0xFF);
                out.push_back((v >> 16) & 0xFF);
                out.push_back((v >> 24) & 0xFF);
            };
            auto writeU64 = [&](uint64_t v) {
                for (int i = 0; i < 8; ++i) {
                    out.push_back((v >> (i * 8)) & 0xFF);
                }
            };

            writeU32(0x4B435342); // 'KCSB'
            out.push_back(2);
            out.push_back(0);
            size_t countPos = out.size();
            writeU32(0);
            uint32_t count = 0;
            for (int y = 0; y < h; ++y) {
                for (int z = 0; z < N; ++z) {
                    for (int x = 0; x < N; ++x) {
                        size_t idx = static_cast<size_t>(y) * N * N + static_cast<size_t>(z) * N + x;
                        uint64_t id = (idx < data.size()) ? data[idx] : 0;
                        if (id == 0) continue;
                        PackedPos p = PackedPos::pack(x, y, z);
                        writeU32(p.value);
                        writeU64(id);
                        ++count;
                    }
                }
            }
            out[countPos] = count & 0xFF;
            out[countPos + 1] = (count >> 8) & 0xFF;
            out[countPos + 2] = (count >> 16) & 0xFF;
            out[countPos + 3] = (count >> 24) & 0xFF;

            return std::string(out.begin(), out.end());
        }

        std::vector<uint64_t> deserialize(std::vector<char>& data) {
            if (data.empty()) return {};
            auto& s = RendererSettings::get();
            int N = s.chunkSize;
            int h = s.worldHeight;

            std::vector<uint64_t> blockData(static_cast<size_t>(N) * h * N, 0);

            if (data.size() < 10) return blockData;
            uint32_t magic = (static_cast<unsigned char>(data[0])) |
                             (static_cast<unsigned char>(data[1]) << 8) |
                             (static_cast<unsigned char>(data[2]) << 16) |
                             (static_cast<unsigned char>(data[3]) << 24);
            if (magic != 0x4B435342) return blockData;
            size_t pos = 4;
            uint16_t version = (static_cast<unsigned char>(data[pos])) |
                               (static_cast<unsigned char>(data[pos + 1]) << 8);
            pos += 2;
            if (pos + 4 > data.size()) return blockData;
            uint32_t count = (static_cast<unsigned char>(data[pos])) |
                             (static_cast<unsigned char>(data[pos + 1]) << 8) |
                             (static_cast<unsigned char>(data[pos + 2]) << 16) |
                             (static_cast<unsigned char>(data[pos + 3]) << 24);
            pos += 4;
            for (uint32_t i = 0; i < count && pos + 12 <= data.size(); ++i) {
                uint32_t packedVal = (static_cast<unsigned char>(data[pos])) |
                                     (static_cast<unsigned char>(data[pos + 1]) << 8) |
                                     (static_cast<unsigned char>(data[pos + 2]) << 16) |
                                     (static_cast<unsigned char>(data[pos + 3]) << 24);
                pos += 4;
                uint64_t id = static_cast<uint64_t>(static_cast<unsigned char>(data[pos])) |
                              (static_cast<uint64_t>(static_cast<unsigned char>(data[pos + 1])) << 8) |
                              (static_cast<uint64_t>(static_cast<unsigned char>(data[pos + 2])) << 16) |
                              (static_cast<uint64_t>(static_cast<unsigned char>(data[pos + 3])) << 24) |
                              (static_cast<uint64_t>(static_cast<unsigned char>(data[pos + 4])) << 32) |
                              (static_cast<uint64_t>(static_cast<unsigned char>(data[pos + 5])) << 40) |
                              (static_cast<uint64_t>(static_cast<unsigned char>(data[pos + 6])) << 48) |
                              (static_cast<uint64_t>(static_cast<unsigned char>(data[pos + 7])) << 56);
                pos += 8;
                if (version == 2) {
                    PackedPos p(packedVal);
                    int x = 0, y = 0, z = 0;
                    p.unpack(x, y, z);
                    if (x < 0 || x >= N || y < 0 || y >= h || z < 0 || z >= N) continue;
                    size_t idx = static_cast<size_t>(y) * N * N + static_cast<size_t>(z) * N + x;
                    if (idx < blockData.size()) {
                        blockData[idx] = id;
                    }
                } else {
                    size_t idx = packedVal;
                    if (idx < blockData.size()) {
                        blockData[idx] = id;
                    }
                }
            }
            return blockData;
        }

    private:
        static int hexDigit(char c) {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }

        std::string buildPath(int gridX, int gridZ) {
            return StringUtils::build("world/chunks/", gridX, "_", gridZ, ".txt");
        }
    };
}
