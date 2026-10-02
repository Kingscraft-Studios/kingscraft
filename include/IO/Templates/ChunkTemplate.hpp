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
        // A chunk's block grid is N*h rows of N cells. Each cell is the encoded
        // uint64 identifier. Two cell encodings are supported, selected by a
        // marker line that immediately follows the chunk's "#C" tag:
        //
        //   (no marker)  flat: one 16-digit lowercase hex token per cell
        //   @F1          run-length: "<count>x<hex>" pairs, 16 cells per row
        //
        // RLE is what makes the text overlay bearable at full Minecraft height:
        // a 384-tall chunk is 6144 rows and nearly all of them are sky, so the
        // flat form spends ~1.6 MB per chunk (1.7 GB for a 32x32 region) writing
        // "0000000000000000" sixteen times, while the run form collapses a whole
        // sky row to "16x0000000000000000".
        //
        // Two details keep this compatible with RegionTemplate: the row count
        // is identical in both encodings, so the grid geometry never changes,
        // and the marker deliberately does NOT start with '#' — region slicing
        // ends at the first '#' line, so a '#F' marker would be dropped and the
        // decoder left guessing. A file with no marker reads as the old flat
        // form, so existing regions still load.
        static constexpr char CELL_ENCODING_PREFIX = '@';
        static constexpr int RLE_VERSION = 1;

        std::string serialize(const std::vector<uint64_t>& data) {
            auto& s = RendererSettings::get();
            int N = s.chunkSize;
            int h = s.worldHeight;

            static const char* HEX = "0123456789abcdef";

            std::string out;
            out.reserve(static_cast<size_t>(N) * h * 8);

            auto appendHex = [&out](uint64_t v) {
                char buf[16];
                for (int i = 15; i >= 0; --i) { buf[i] = HEX[v & 0xF]; v >>= 4; }
                out.append(buf, 16);
            };

            out += CELL_ENCODING_PREFIX;
            out += 'F';
            out += static_cast<char>('0' + RLE_VERSION);
            out += '\n';

            for (int y = 0; y < h; ++y) {
                if (y > 0) out += '\n';

                for (int z = 0; z < N; ++z) {
                    // Emit runs across the whole row; a run may span a slice
                    // boundary, which is harmless because the decoder expands
                    // by count and re-wraps at N cells.
                    int x = 0;
                    bool first = true;
                    while (x < N) {
                        size_t base = static_cast<size_t>(y) * N * N
                                    + static_cast<size_t>(z) * N + x;
                        uint64_t id = (base < data.size()) ? data[base] : 0;
                        int run = 1;
                        while (x + run < N) {
                            size_t j = static_cast<size_t>(y) * N * N
                                     + static_cast<size_t>(z) * N + (x + run);
                            uint64_t v = (j < data.size()) ? data[j] : 0;
                            if (v != id) break;
                            ++run;
                        }
                        if (!first) out += ' ';
                        first = false;
                        // Run count in decimal, written by hand: to_string on
                        // every cell of a 98304-cell grid showed up in profiles,
                        // and a run can never exceed chunkSize anyway.
                        {
                            char digits[12];
                            int n = 0;
                            for (int v = run; v > 0; v /= 10) digits[n++] = HEX[v % 10];
                            while (n > 0) out += digits[--n];
                        }
                        out += 'x';
                        appendHex(id);
                        x += run;
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

            // Default to the original flat encoding; the marker flips it. Local
            // (not a member) so concurrent loads of different regions cannot
            // race on it.
            bool rle = false;

            while (pos < content.size() && dataLine < h * N) {
                size_t eol = content.find('\n', pos);
                if (eol == std::string::npos) eol = content.size();

                std::string line = content.substr(pos, eol - pos);
                pos = eol + 1;

                if (line.empty()) continue;
                if (line[0] == '#') continue;
                if (line[0] == CELL_ENCODING_PREFIX) {
                    rle = (line.size() >= 3 && line[1] == 'F' &&
                           (line[2] - '0') == RLE_VERSION);
                    continue;   // metadata, not a grid row
                }

                int y = dataLine / N;
                int z = dataLine % N;
                size_t rowBase = static_cast<size_t>(y) * N * N
                               + static_cast<size_t>(z) * N;

                if (rle) {
                    // "<count>x<hex>" pairs, one row per line.
                    size_t i = 0;
                    int x = 0;
                    while (i < line.size() && x < N) {
                        if (line[i] == ' ') { ++i; continue; }
                        int run = 0;
                        while (i < line.size() && line[i] >= '0' && line[i] <= '9')
                            run = run * 10 + (line[i++] - '0');
                        if (run <= 0) break;
                        if (i >= line.size() || line[i] != 'x') break;
                        ++i;   // 'x'
                        uint64_t id = 0;
                        for (int d = 0; d < 16 && i < line.size(); ++d, ++i) {
                            int digit = hexDigit(line[i]);
                            if (digit < 0) break;
                            id = (id << 4) | static_cast<uint64_t>(digit);
                        }
                        for (int c = 0; c < run && x < N; ++c, ++x) {
                            size_t idx = rowBase + x;
                            if (idx < blockData.size()) blockData[idx] = id;
                        }
                    }
                } else {
                    std::istringstream iss(line);
                    for (int x = 0; x < N; ++x) {
                        uint64_t id = 0;
                        if (!(iss >> std::hex >> id)) break;
                        size_t idx = rowBase + x;
                        if (idx < blockData.size())
                            blockData[idx] = id;
                    }
                }
                dataLine++;
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
            return StringBuilder::build("world/chunks/", gridX, "_", gridZ, ".txt");
        }
    };
}
