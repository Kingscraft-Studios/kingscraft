#pragma once
#include "IOTemplateBase.hpp"
#include "ChunkTemplate.hpp"
#include "Util/StringBuilder.hpp"

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace kc {
    // Stores 32x32 chunks in one file. Chunk identity lives on a "#C <gx> <gz>"
    // tag line; the block-grid text that follows it is exactly what
    // ChunkTemplate::serialize produces.
    //
    //   world/regions/r.<regX>.<regZ>.txt:
    //     #R <regX> <regZ>
    //     #C <gx> <gz>
    //     <h*N block-grid lines, N chars each>
    //     #C <gx2> <gz2>
    //     ...
    //
    // regionX/Z use flooring division so negative chunk coords land right.
    //
    // Performance model: a region file is read from disk at most ONCE (its
    // text stays cached in memory for the session). Chunk lookups and edits
    // splice the in-memory line buffer directly, and flush() writes each
    // region file once per batch — never one full-file rewrite per chunk.
    class RegionTemplate final : public IOTemplateBase {
    public:
        static constexpr int REGION_SIZE = 32;      // 32 x 32 chunks per region

        explicit RegionTemplate(IO& io, ChunkTemplate& chunkTemplate)
            : IOTemplateBase(io), chunk_(chunkTemplate) {}

        std::vector<uint64_t> load(int gridX, int gridZ) {
            std::lock_guard<std::mutex> lock(mutex_);
            uint64_t k = regionKeyOfChunk(gridX, gridZ);

            // Staged-but-unflushed edits must be visible to readers.
            auto pendingIt = pending_.find(k);
            if (pendingIt != pending_.end()) {
                auto stagedIt = pendingIt->second.find(chunkKey(gridX, gridZ));
                if (stagedIt != pendingIt->second.end()) {
                    std::vector<char> payload(stagedIt->second.begin(), stagedIt->second.end());
                    return chunk_.deserialize(payload);
                }
            }

            RegionState& s = stateFor(k);
            auto hit = s.tagIdx.find(chunkKey(gridX, gridZ));
            if (hit == s.tagIdx.end()) return {};

            std::string text = sliceChunkText(s, hit->second);
            std::vector<char> payload(text.begin(), text.end());
            return chunk_.deserialize(payload);
        }

        // Immediate write (rarely needed; most callers prefer queueSave + flush).
        void save(int gridX, int gridZ, const std::vector<uint64_t>& data) {
            std::lock_guard<std::mutex> lock(mutex_);
            uint64_t k = regionKeyOfChunk(gridX, gridZ);
            pending_[k][chunkKey(gridX, gridZ)] = chunk_.serialize(data);
            stateFor(k);      // pulls the edit into the region text
            flushRegion(k);   // and hands it straight to disk
        }

        // Stage a chunk for saving. Same chunk queued twice collapses to the
        // newest text (only the last edit reaches disk). Memory-only: the
        // disk is touched once per region by flush() — synchronous per-chunk
        // rewrites of the whole region file here were the shutdown bottleneck.
        void queueSave(int gridX, int gridZ, const std::vector<uint64_t>& data) {
            std::lock_guard<std::mutex> lock(mutex_);
            uint64_t k = regionKeyOfChunk(gridX, gridZ);
            pending_[k][chunkKey(gridX, gridZ)] = chunk_.serialize(data);
        }

        // Write every dirty region file once. Idempotent: clean regions cost
        // nothing, so calling this in a loop (or again right after) is cheap.
        // This is the ONLY place disk writes happen now (IO::Shutdown).
        void flush() {
            std::lock_guard<std::mutex> lock(mutex_);
            std::vector<uint64_t> keys;
            keys.reserve(states_.size() + pending_.size());
            for (const auto& [k, s] : states_) if (s.loaded) keys.push_back(k);
            for (const auto& [k, p] : pending_) if (!p.empty()) keys.push_back(k);
            std::sort(keys.begin(), keys.end());
            keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
            for (uint64_t k : keys) flushRegion(k);
        }

    private:
        struct RegionState {
            std::vector<std::string> lines;                  // whole region text, one string per line
            std::unordered_map<uint64_t, int32_t> tagIdx;    // chunkKey -> index of its "#C" line
            uint64_t accessStamp_ = 0;                       // LRU stamp, bumped on every stateFor use
            bool loaded = false;
            bool dirty = false;
        };

        ChunkTemplate& chunk_;
        std::mutex mutex_;
        std::unordered_map<uint64_t, RegionState> states_;
        std::unordered_map<uint64_t, std::unordered_map<uint64_t, std::string>> pending_;
        uint64_t accessCounter_ = 0;

        static uint64_t regionKey(int regX, int regZ) {
            return (static_cast<uint64_t>(static_cast<int64_t>(regX)) << 32) |
                   (static_cast<uint64_t>(static_cast<int64_t>(regZ)) & 0xFFFFFFFF);
        }

        static uint64_t chunkKey(int gx, int gz) {
            return (static_cast<uint64_t>(static_cast<int64_t>(gx)) << 32) |
                   (static_cast<uint64_t>(static_cast<int64_t>(gz)) & 0xFFFFFFFF);
        }

        static uint64_t regionKeyOfChunk(int gridX, int gridZ) {
            return regionKey(floorDiv(gridX, REGION_SIZE), floorDiv(gridZ, REGION_SIZE));
        }

        static int floorDiv(int a, int b) {
            int q = a / b;
            int r = a % b;
            if (r < 0) { r += b; q -= 1; }
            return q;
        }

        std::string builtPath(int regX, int regZ) {
            return StringBuilder::build("world/regions/r.", regX, ".", regZ, ".txt");
        }

        // Lazily materialize + load a region, pull its staged edits in, and
        // bound the in-memory cache. The returned reference stays valid until
        // the next stateFor/erase call.
        RegionState& stateFor(uint64_t k) {
            auto [it, inserted] = states_.try_emplace(k);
            RegionState& s = it->second;
            if (!s.loaded) {
                int rX = static_cast<int>(k >> 32);
                int rZ = static_cast<int>(k & 0xFFFFFFFF);
                auto fileData = readBytes(builtPath(rX, rZ));
                if (fileData.empty())
                    s.lines = {StringBuilder::build("#R ", rX, " ", rZ)};
                else
                    s.lines = splitLines(std::string(fileData.begin(), fileData.end()));
                rebuildTagIndex(s);
                s.loaded = true;
            }
            applyPending(k, s);
            s.accessStamp_ = ++accessCounter_;   // touch: this region is most recently used
            evictIfNeeded(k);
            return s;
        }

        void flushRegion(uint64_t k) {
            RegionState& s = stateFor(k);
            if (s.dirty) persistRegion(k, s);
        }

        void persistRegion(uint64_t k, RegionState& s) {
            if (!s.dirty) return;
            std::string out;
            for (const auto& line : s.lines) {
                if (!out.empty()) out += '\n';
                out += line;
            }
            int rX = static_cast<int>(k >> 32);
            int rZ = static_cast<int>(k & 0xFFFFFFFF);
            writeBytes(builtPath(rX, rZ), std::vector<char>(out.begin(), out.end()));
            s.dirty = false;
        }

        void applyPending(uint64_t k, RegionState& s) {
            auto pendingIt = pending_.find(k);
            if (pendingIt == pending_.end() || pendingIt->second.empty()) return;

            struct Operation {
                int32_t idx;                       // tag line index, or -1 to append
                uint64_t ck;
                std::string tag;
                std::vector<std::string> data;
            };
            std::vector<Operation> ops;
            ops.reserve(pendingIt->second.size());
            for (const auto& [ck, text] : pendingIt->second) {
                auto tagIt = s.tagIdx.find(ck);
                ops.push_back(Operation{
                    (tagIt != s.tagIdx.end()) ? tagIt->second : -1,
                    ck,
                    chunkTag(ck),
                    splitLines(text)
                });
            }
            // Replaces first (descending so earlier indices never shift), then
            // appends (they only add at the end, invalidating nothing).
            std::sort(ops.begin(), ops.end(),
                      [](const Operation& a, const Operation& b) { return a.idx > b.idx; });

            for (const auto& op : ops) {
                if (op.idx >= 0) {
                    int end = op.idx + 1;
                    while (end < static_cast<int>(s.lines.size()) && !isDirective(s.lines[end])) ++end;
                    s.lines.erase(s.lines.begin() + op.idx, s.lines.begin() + end);
                    s.lines.insert(s.lines.begin() + op.idx, op.tag);
                    s.lines.insert(s.lines.begin() + op.idx + 1, op.data.begin(), op.data.end());
                    s.tagIdx[op.ck] = op.idx;
                } else {
                    int span = static_cast<int>(op.data.size());
                    s.lines.push_back(op.tag);
                    s.lines.insert(s.lines.end(), op.data.begin(), op.data.end());
                    s.tagIdx[op.ck] = static_cast<int32_t>(s.lines.size() - span - 1);
                }
            }
            s.dirty = true;
            pending_.erase(pendingIt);
        }

        // Evict the least-recently-used regions down to the configured cap
        // (RendererSettings::regionCacheLimit). Dirty regions are persisted
        // before their in-memory text is dropped. The just-touched region has
        // the newest stamp and is never a target.
        void evictIfNeeded(uint64_t keep) {
            while (static_cast<int>(states_.size()) > RendererSettings::get().regionCacheLimit) {
                uint64_t oldestKey = keep;
                uint64_t oldestStamp = ~0ull;
                for (const auto& [k, s] : states_) {
                    if (k == keep) continue;
                    if (s.accessStamp_ < oldestStamp) {
                        oldestStamp = s.accessStamp_;
                        oldestKey = k;
                    }
                }
                auto it = states_.find(oldestKey);
                if (it == states_.end()) break;   // only 'keep' left — nothing to drop
                if (it->second.dirty) persistRegion(oldestKey, it->second);
                states_.erase(it);
            }
        }

        static std::string chunkTag(uint64_t ck) {
            int gx = static_cast<int>(ck >> 32);
            int gz = static_cast<int>(ck & 0xFFFFFFFF);
            return StringBuilder::build("#C ", gx, " ", gz);
        }

        static std::string sliceChunkText(const RegionState& s, int tagIndex) {
            std::string out;
            for (int j = tagIndex + 1; j < static_cast<int>(s.lines.size()) && !isDirective(s.lines[j]); ++j) {
                out += s.lines[j];
                out += '\n';
            }
            return out;
        }

        static void rebuildTagIndex(RegionState& s) {
            s.tagIdx.clear();
            for (int i = 0; i < static_cast<int>(s.lines.size()); ++i) {
                int gx = 0, gz = 0;
                if (parseChunkTag(s.lines[i], gx, gz))
                    s.tagIdx[chunkKey(gx, gz)] = i;
            }
        }

        static std::vector<std::string> splitLines(const std::string& text) {
            std::vector<std::string> lines;
            size_t pos = 0;
            while (pos <= text.size()) {
                size_t eol = text.find('\n', pos);
                std::string line = (eol == std::string::npos)
                    ? text.substr(pos)
                    : text.substr(pos, eol - pos);
                if (!line.empty()) lines.push_back(line);
                if (eol == std::string::npos) break;
                pos = eol + 1;
            }
            return lines;
        }

        static bool parseChunkTag(const std::string& line, int& gx, int& gz) {
            std::istringstream iss(line);
            std::string tag;
            if (!(iss >> tag >> gx >> gz)) return false;
            return tag == "#C";
        }

        static bool isDirective(const std::string& line) {
            return !line.empty() && line[0] == '#';
        }
    };
}