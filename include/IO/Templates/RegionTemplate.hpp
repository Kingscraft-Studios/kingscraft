#pragma once
#include "IOTemplateBase.hpp"
#include "ChunkTemplate.hpp"
#include "IO/Serialization/KcrFormat.hpp"
#include "Renderer/RendererSettings.hpp"
#include "Util/LogUtils.hpp"
#include "Util/StringUtils.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace kc {
    // Stores 32x32 chunks in one binary file. Chunk identity lives in the
    // region directory (index = localX + localZ * 32) plus a per-chunk "KCH"
    // header, replacing the old "#R"/"#C" tag lines of the text format. The
    // chunk payload itself is unchanged: it is exactly the bytes produced by
    // ChunkTemplate::serialize.
    //
    //   world/regions/r.<regX>.<regZ>.kcr:
    //     KcrHeader
    //     DirEntry[1024]        (one per chunk slot, FLAG_PRESENT when used,
    //                            reserved bytes carry a per-entry CRC32)
    //     <KCH header + text payload> records...
    //
    // regionX/Z use flooring division so negative chunk coords land right.
    //
    // Phase 2 = single-record random access:
    //   read  -> cache only the 24 KB directory per region, then one
    //            positional read of the one record asked for. The whole
    //            region file is never read into memory, and a chunk that
    //            was never saved costs a directory lookup and nothing else.
    //   write -> append the dirty records at EOF and patch only their
    //            directory entries; nothing already in the file is touched
    //            (write only the chunk). The bytes of a chunk's previous
    //            record are left as unreachable garbage the directory no
    //            longer points at -- reclaiming them is Phase 4 compaction.
    //
    // Save reliability (R1/R2): writes are confirmed (DiskOperations checks the
    // stream) and staged records are only dropped once every required write --
    // records, directory, header count -- has succeeded; a failure leaves the
    // region dirty so the edit is not silently lost. Ordering is records first,
    // then directory, then count, and each directory entry carries a CRC so a
    // torn directory rewrite is detected (the untouched entries are
    // byte-identical and stay valid) rather than trusted. This is not a full
    // durability guarantee: there is no fsync and no journal, so a power loss
    // can still lose the most recent save.
    // The on-disk layout does not change, so Phase-1 files stay readable and
    // tools/terrain_analyzer.py needs no change.
    class RegionTemplate final : public IOTemplateBase {
    public:
        static constexpr int REGION_SIZE = kcr::REGION_SIZE;      // 32 x 32 chunks per region

        explicit RegionTemplate(DiskLayer& disk, ChunkTemplate& chunkTemplate)
            : IOTemplateBase(disk), chunk_(chunkTemplate) {}

        // Coarse contention counters for the region lock (R4). loadCalls is the
        // number of load() calls; lockWaitNs is the total time spent waiting to
        // acquire the template mutex. Exercised by /tmp harnesses, not a
        // correctness path.
        uint64_t getLoadCalls() const { return loadCalls_.load(std::memory_order_relaxed); }
        uint64_t getLoadLockWaitNs() const { return loadLockWaitNs_.load(std::memory_order_relaxed); }

        std::vector<uint64_t> load(int gridX, int gridZ) {
            loadCalls_.fetch_add(1, std::memory_order_relaxed);

            const auto waitStart = std::chrono::steady_clock::now();
            std::unique_lock<std::mutex> lock(mutex_);
            loadLockWaitNs_.fetch_add(
                static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - waitStart).count()),
                std::memory_order_relaxed);

            uint64_t k = regionKeyOfChunk(gridX, gridZ);
            uint32_t li = localIndex(gridX, gridZ);

            // Staged-but-unflushed edits must be visible to readers. Checked
            // first so reading back a just-queued save materializes nothing.
            auto pendingIt = pending_.find(k);
            if (pendingIt != pending_.end()) {
                auto stagedIt = pendingIt->second.find(li);
                if (stagedIt != pendingIt->second.end()) {
                    std::vector<char> rec = stagedIt->second;   // copy before unlock
                    lock.unlock();
                    return decodeRecord(rec.data(), rec.size(), gridX, gridZ);
                }
            }

            RegionState& s = stateFor(k);

            // Edited but not appended yet: the newest bytes are still in memory.
            auto stagedRec = s.staged.find(li);
            if (stagedRec != s.staged.end()) {
                std::vector<char> rec = stagedRec->second;       // copy before unlock
                lock.unlock();
                return decodeRecord(rec.data(), rec.size(), gridX, gridZ);
            }

            // Clean chunk: directory lookup -> one positional read of the record.
            const kcr::DirEntry& e = s.dir[li];
            if (!(e.flags & kcr::FLAG_PRESENT) || e.size == 0)
                return {};                       // never saved -> caller regenerates
            if (e.offset < kcr::REGION_DATA_OFFSET)
                return {};                       // impossible for a real record

            const std::string path = builtPath(regionXOfKey(k), regionZOfKey(k));
            const std::size_t recOffset = static_cast<std::size_t>(e.offset);
            const std::size_t recSize = e.size;
            lock.unlock();

            // Read and decode outside the template mutex so a load no longer
            // blocks another region's load (or the prefetch worker) for the
            // whole file read + text decode. The file only grows in-process, so
            // the resolved offset/size stay valid. The decode only touches
            // stateless codec code, so it is safe to run unlocked too.
            const std::size_t fsize = fileSize(path);
            // The file only grows in-process, so this only trips on external
            // truncation. Fail safe (regenerate) instead of reading a short record.
            if (recOffset > fsize || recSize > fsize - recOffset)
                return {};

            auto rec = readBytes(path, recOffset, recSize);
            if (!rec) return {};
            return decodeRecord(rec->data(), rec->size(), gridX, gridZ);
        }

        // Immediate write (rarely needed; most callers prefer queueSave + flush).
        void save(int gridX, int gridZ, const std::vector<uint64_t>& data) {
            std::lock_guard<std::mutex> lock(mutex_);
            uint64_t k = regionKeyOfChunk(gridX, gridZ);
            pending_[k][localIndex(gridX, gridZ)] = makeRecord(gridX, gridZ, data);
            stateFor(k);      // pulls the edit into the region state
            if (!flushRegion(k))
                LogUtils::error(ThreadName::Engine,
                    StringUtils::build("KCR save failed (r.", regionXOfKey(k), ".",
                                        regionZOfKey(k), ".kcr)"));
        }

        // Stage a chunk for saving. Same chunk queued twice collapses to the
        // newest record (only the last edit reaches disk). Memory-only: the
        // disk is touched once per region by flush().
        void queueSave(int gridX, int gridZ, const std::vector<uint64_t>& data) {
            std::lock_guard<std::mutex> lock(mutex_);
            uint64_t k = regionKeyOfChunk(gridX, gridZ);
            pending_[k][localIndex(gridX, gridZ)] = makeRecord(gridX, gridZ, data);
        }

        // Append every dirty region's staged records. Idempotent: clean regions
        // cost nothing, so calling this in a loop (or again right after) is
        // cheap. Returns false if any region failed to persist (its staged
        // records are kept dirty). This is the ONLY place disk writes happen now.
        bool flush() {
            std::lock_guard<std::mutex> lock(mutex_);
            std::vector<uint64_t> keys;
            keys.reserve(states_.size() + pending_.size());
            for (const auto& [k, s] : states_) if (s.loaded) keys.push_back(k);
            for (const auto& [k, p] : pending_) if (!p.empty()) keys.push_back(k);
            std::sort(keys.begin(), keys.end());
            keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
            bool ok = true;
            for (uint64_t k : keys) if (!flushRegion(k)) ok = false;
            logStorageStatsLocked();
            return ok;
        }

    private:
        struct RegionState {
            // Whole directory, cached so no chunk read re-reads it. Slots the
            // file never used stay zero-filled (flags == 0). 24 KB per region.
            std::array<kcr::DirEntry, kcr::CHUNK_STRIDE> dir{};
            // Dirty records waiting to be appended: localIndex -> KCH record.
            std::unordered_map<uint32_t, std::vector<char>> staged;
            uint32_t presentCount = 0;   // slots with FLAG_PRESENT == header chunkCount
            uint64_t accessStamp_ = 0;   // LRU stamp, bumped on every stateFor use
            bool loaded = false;         // directory has been read (or file found absent)
            bool headerOk = false;       // on-disk header exists and validated
            bool dirty = false;
        };

        ChunkTemplate& chunk_;
        std::mutex mutex_;
        std::unordered_map<uint64_t, RegionState> states_;
        std::unordered_map<uint64_t, std::unordered_map<uint32_t, std::vector<char>>> pending_;
        uint64_t accessCounter_ = 0;
        std::atomic<uint64_t> loadCalls_{0};
        std::atomic<uint64_t> loadLockWaitNs_{0};

        // Encode a chunk's block grid through the (unchanged) text codec and
        // wrap it in a KCH record.
        std::vector<char> makeRecord(int gridX, int gridZ, const std::vector<uint64_t>& data) {
            std::string text = chunk_.serialize(data);
            std::vector<char> payload(text.begin(), text.end());
            return kcr::buildChunkRecord(gridX, gridZ, kcr::CHUNK_STATE_MODIFIED,
                                         static_cast<uint16_t>(ChunkTemplate::RLE_VERSION),
                                         payload);
        }

        // Shared tail of every read: validate the KCH header (magic, version and
        // the duplicated chunk coords) and hand the payload to the text codec.
        std::vector<uint64_t> decodeRecord(const char* record, std::size_t recordSize,
                                           int gridX, int gridZ) {
            std::vector<char> payload;
            if (!kcr::extractChunkPayload(record, recordSize, gridX, gridZ, payload)) {
                LogUtils::warn(ThreadName::Engine,
                    StringUtils::build("KCR bad chunk record, ignoring (", gridX, ",",
                                        gridZ, ")"));
                return {};
            }
            return chunk_.deserialize(payload);
        }

        static uint64_t regionKey(int regX, int regZ) {
            return (static_cast<uint64_t>(static_cast<int64_t>(regX)) << 32) |
                   (static_cast<uint64_t>(static_cast<int64_t>(regZ)) & 0xFFFFFFFF);
        }

        static int regionXOfKey(uint64_t k) { return static_cast<int>(static_cast<int64_t>(k >> 32)); }
        static int regionZOfKey(uint64_t k) {
            return static_cast<int>(static_cast<int32_t>(k & 0xFFFFFFFF));
        }

        static uint64_t regionKeyOfChunk(int gridX, int gridZ) {
            return regionKey(floorDiv(gridX, REGION_SIZE), floorDiv(gridZ, REGION_SIZE));
        }

        static uint32_t localIndex(int gridX, int gridZ) {
            int lx = gridX - floorDiv(gridX, REGION_SIZE) * REGION_SIZE;
            int lz = gridZ - floorDiv(gridZ, REGION_SIZE) * REGION_SIZE;
            return static_cast<uint32_t>(lx + lz * REGION_SIZE);
        }

        static int floorDiv(int a, int b) {
            int q = a / b;
            int r = a % b;
            if (r < 0) { r += b; q -= 1; }
            return q;
        }

        std::string builtPath(int regX, int regZ) {
            return StringUtils::build("world/regions/r.", regX, ".", regZ, ".kcr");
        }

        // Lazily materialize a region (header + directory only, 24 KB), pull its
        // staged edits in, and bound the cache. The returned reference stays
        // valid until the next stateFor/erase call.
        RegionState& stateFor(uint64_t k) {
            auto [it, inserted] = states_.try_emplace(k);
            RegionState& s = it->second;
            if (!s.loaded) {
                loadDirectory(k, s);
                s.loaded = true;
            }
            applyPending(k, s);
            s.accessStamp_ = ++accessCounter_;   // touch: this region is most recently used
            evictIfNeeded(k);
            return s;
        }

        // Read just the 24 KB header+directory of a region and cache it. A
        // missing file is a brand-new region (no error). Corrupt, truncated or
        // wrong-coords files are rejected with a warning and treated as empty
        // rather than crashing.
        void loadDirectory(uint64_t k, RegionState& s) {
            int rX = regionXOfKey(k);
            int rZ = regionZOfKey(k);
            const std::string path = builtPath(rX, rZ);

            const std::size_t size = fileSize(path);
            if (size == 0) return;   // brand-new region
            if (size < kcr::REGION_DATA_OFFSET) {
                LogUtils::warn(ThreadName::Engine,
                    StringUtils::build("KCR too small, ignoring (r.", rX, ".", rZ, ".kcr)"));
                return;
            }

            auto head = readBytes(path, 0, kcr::REGION_DATA_OFFSET);
            if (!head || head->size() < kcr::REGION_DATA_OFFSET) {
                LogUtils::warn(ThreadName::Engine,
                    StringUtils::build("KCR header read failed (r.", rX, ".", rZ, ".kcr)"));
                return;
            }

            BinaryReader r(head->data(), head->size());
            kcr::KcrHeader h;
            if (!kcr::readRegionHeader(r, h)) {
                LogUtils::warn(ThreadName::Engine,
                    StringUtils::build("KCR bad header, ignoring (r.", rX, ".", rZ, ".kcr)"));
                return;
            }
            if (h.regionX != rX || h.regionZ != rZ) {
                LogUtils::warn(ThreadName::Engine,
                    StringUtils::build("KCR region coords mismatch, ignoring (r.", rX, ".",
                                        rZ, ".kcr)"));
                return;
            }
            s.headerOk = true;

            for (std::size_t i = 0; i < kcr::CHUNK_STRIDE; ++i) {
                kcr::DirEntry& e = s.dir[i];
                bool crcOk = true;
                if (!kcr::readDirEntry(r, e, crcOk)) break;
                if (!crcOk) {
                    LogUtils::warn(ThreadName::Engine,
                        StringUtils::build("KCR directory entry CRC mismatch ", i, " (r.", rX, ".",
                                            rZ, ".kcr)"));
                    e = kcr::DirEntry{};
                    continue;
                }
                if (!(e.flags & kcr::FLAG_PRESENT) || e.size == 0) continue;
                // Bounds against the file as it exists right now. The file only
                // grows in this process, so an out-of-range entry here is on-disk
                // corruption: report it and empty the slot instead of trusting it.
                if (e.offset < kcr::REGION_DATA_OFFSET || e.offset > size ||
                    e.size > size - e.offset) {
                    LogUtils::warn(ThreadName::Engine,
                        StringUtils::build("KCR bad directory entry ", i, " (r.", rX, ".",
                                            rZ, ".kcr)"));
                    e = kcr::DirEntry{};
                    continue;
                }
                ++s.presentCount;
            }
        }

        bool flushRegion(uint64_t k) {
            RegionState& s = stateFor(k);
            if (!s.dirty) return true;
            return persistRegion(k, s);
        }

        // Append the dirty chunks. Records first, then the directory, then the
        // header count. Per-entry CRCs allow detecting a torn directory rewrite;
        // the appended records are unreferenced only if the directory write fails,
        // so this ordering does not make a guarantee of durability. Returns false
        // on write failure (staged remains dirty in that case).
        bool persistRegion(uint64_t k, RegionState& s) {
            if (s.staged.empty()) { s.dirty = false; return true; }

            int rX = regionXOfKey(k);
            int rZ = regionZOfKey(k);
            const std::string path = builtPath(rX, rZ);

            // Step 0: make sure a valid header + directory exist to append into.
            // Rebuild covers three cases: missing file, truncated file, and a
            // header that failed validation on load (otherwise we'd append into
            // a container whose magic/coords are still wrong and unreadable).
            std::size_t base = fileSize(path);
            if (base < kcr::REGION_DATA_OFFSET || !s.headerOk) {
                if (base > kcr::REGION_DATA_OFFSET)
                    LogUtils::warn(ThreadName::Engine,
                        StringUtils::build("KCR invalid header, rebuilding (r.", rX, ".", rZ,
                                            ".kcr)"));
                else if (base > 0)
                    LogUtils::warn(ThreadName::Engine,
                        StringUtils::build("KCR truncated, rebuilding (r.", rX, ".", rZ,
                                            ".kcr)"));
                // The cache is rebuilt with the file: unreadable old slots are gone.
                s.dir = {};
                s.presentCount = 0;
                kcr::KcrHeader h;
                h.regionX = rX;
                h.regionZ = rZ;
                h.chunkCount = s.presentCount;
                std::vector<char> fresh;
                fresh.reserve(kcr::REGION_DATA_OFFSET);
                BinaryWriter w(fresh);
                kcr::writeRegionHeader(w, h);
                fresh.insert(fresh.end(), kcr::DIRECTORY_SIZE, 0);
                if (!writeBytes(path, fresh)) {
                    LogUtils::error(ThreadName::Engine,
                        StringUtils::build("KCR cannot create region file (r.", rX, ".", rZ, ".kcr)"));
                    return false;
                }
                base = kcr::REGION_DATA_OFFSET;
                s.headerOk = true;
            }

            // Step 1: append the staged records contiguously at EOF.
            std::vector<uint32_t> indices;
            indices.reserve(s.staged.size());
            for (const auto& [li, rec] : s.staged) indices.push_back(li);
            std::sort(indices.begin(), indices.end());

            std::size_t blobSize = 0;
            for (uint32_t li : indices) blobSize += s.staged[li].size();
            std::vector<char> blob;
            blob.reserve(blobSize);

            for (uint32_t li : indices) {
                const std::vector<char>& rec = s.staged[li];
                kcr::DirEntry& e = s.dir[li];
                e.offset = base + blob.size();
                e.size = static_cast<uint32_t>(rec.size());
                e.rawSize = e.size;
                if (!(e.flags & kcr::FLAG_PRESENT)) {
                    e.flags = kcr::FLAG_PRESENT;
                    ++s.presentCount;
                }
                blob.insert(blob.end(), rec.begin(), rec.end());
            }
            if (!writeBytes(path, base, blob)) {
                LogUtils::error(ThreadName::Engine,
                    StringUtils::build("KCR record append failed (r.", rX, ".", rZ, ".kcr)"));
                return false;
            }

            // Step 2: patch the whole directory (one write) so every slot's
            // offset is exact, then step 3: the header's chunkCount.
            if (!writeBytes(path, kcr::REGION_HEADER_SIZE,
                            kcr::serializeDirectory(s.dir.data(), s.dir.size()))) {
                LogUtils::error(ThreadName::Engine,
                    StringUtils::build("KCR directory update failed (r.", rX, ".", rZ, ".kcr)"));
                return false;
            }
            std::vector<char> countBytes;
            BinaryWriter cw(countBytes);
            cw.u32(s.presentCount);
            if (!writeBytes(path, kcr::CHUNK_COUNT_OFFSET, countBytes)) {
                LogUtils::error(ThreadName::Engine,
                    StringUtils::build("KCR header count update failed (r.", rX, ".", rZ, ".kcr)"));
                return false;
            }

            s.staged.clear();
            s.dirty = false;
            return true;
        }

        void applyPending(uint64_t k, RegionState& s) {
            auto pendingIt = pending_.find(k);
            if (pendingIt == pending_.end() || pendingIt->second.empty()) return;
            for (auto& [li, record] : pendingIt->second) {
                s.staged[li] = std::move(record);
            }
            s.dirty = true;
            pending_.erase(pendingIt);
        }

        // Evict the least-recently-used regions down to the configured cap
        // (RendererSettings::regionCacheLimit). Staged records are appended to
        // disk before their region is dropped. If persisting fails, the region is
        // kept in cache (rather than discarding edits) and eviction stops for
        // this pass.
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
                if (it->second.dirty && !persistRegion(oldestKey, it->second)) {
                    LogUtils::error(ThreadName::Engine,
                        StringUtils::build("KCR region eviction deferred: save failed (r.",
                                            regionXOfKey(oldestKey), ".", regionZOfKey(oldestKey),
                                            ".kcr)"));
                    break;
                }
                states_.erase(it);
            }
        }

        void logStorageStatsLocked() {
            std::size_t physical = 0, live = 0;
            int regions = 0;
            for (auto& [k, s] : states_) {
                if (!s.loaded || !s.headerOk) continue;
                ++regions;
                const std::string path = builtPath(regionXOfKey(k), regionZOfKey(k));
                std::size_t fs = fileSize(path);
                physical += fs;
                std::size_t l = kcr::REGION_DATA_OFFSET;
                for (const auto& e : s.dir) if (e.flags & kcr::FLAG_PRESENT) l += e.size;
                live += l;
            }
            if (physical > 0 && physical > live) {
                LogUtils::info(ThreadName::Engine,
                    StringUtils::build("KCR regions: ", regions, " physical=",
                                        static_cast<unsigned long long>(physical),
                                        " live=", static_cast<unsigned long long>(live),
                                        " garbage=", static_cast<unsigned long long>(physical - live)));
            } else if (regions > 0) {
                LogUtils::info(ThreadName::Engine,
                    StringUtils::build("KCR regions: ", regions, " physical=",
                                        static_cast<unsigned long long>(physical),
                                        " live=", static_cast<unsigned long long>(live)));
            }
        }
    };
}
