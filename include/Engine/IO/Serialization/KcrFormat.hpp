#pragma once

#include "BinaryReader.hpp"
#include "BinaryWriter.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace kc::kcr {
    // KCF-1 style container for phase 1: a binary region file that wraps the
    // existing text chunk codec. Chunk identity lives in the region directory
    // plus a per-chunk KCH header, replacing the old "#R"/"#C" tag lines. The
    // chunk payload itself is still ChunkTemplate::serialize output byte for
    // byte — no codec change here.
    //
    //   r.<regX>.<regZ>.kcr
    //     KcrHeader  (32 bytes)
    //     DirEntry[1024]  (24 bytes each, index = localX + localZ * 32;
    //                      reserved bytes carry a per-entry CRC32)
    //     records...  (KCH header + payload)
    //
    // All fields are explicit little-endian; nothing is a raw struct dump.

    inline constexpr char REGION_MAGIC[4] = {'K', 'C', 'R', 0x01};
    inline constexpr char CHUNK_MAGIC[4]  = {'K', 'C', 'H', 0x01};

    inline constexpr uint16_t REGION_FORMAT_VERSION = 1;
    inline constexpr uint16_t CHUNK_FORMAT_VERSION  = 1;

    inline constexpr int REGION_SIZE  = 32;                              // 32 x 32 chunks
    inline constexpr int CHUNK_STRIDE = REGION_SIZE * REGION_SIZE;       // 1024 slots

    inline constexpr uint8_t FLAG_PRESENT = 0x01;
    // Chunk state, mirroring the reserved header field (unused until a later
    // phase decides storage behaviour from it).
    inline constexpr uint8_t CHUNK_STATE_GENERATED = 0;
    inline constexpr uint8_t CHUNK_STATE_MODIFIED  = 1;

    inline constexpr std::size_t REGION_HEADER_SIZE = 32;
    inline constexpr std::size_t DIR_ENTRY_SIZE     = 24;
    inline constexpr std::size_t CHUNK_HEADER_SIZE  = 22;
    inline constexpr std::size_t DIRECTORY_SIZE     = static_cast<std::size_t>(CHUNK_STRIDE) * DIR_ENTRY_SIZE;
    inline constexpr std::size_t REGION_DATA_OFFSET = REGION_HEADER_SIZE + DIRECTORY_SIZE;

    struct KcrHeader {
        uint16_t version = REGION_FORMAT_VERSION;
        int32_t  regionX = 0;
        int32_t  regionZ = 0;
        uint32_t chunkCount = 0;
    };

    struct DirEntry {
        uint64_t offset = 0;   // absolute byte offset of the chunk record
        uint32_t size = 0;     // stored record size in bytes
        uint32_t rawSize = 0;  // uncompressed size (== size while uncompressed)
        uint8_t  flags = 0;    // FLAG_PRESENT
    };

    struct KchHeader {
        uint16_t version = CHUNK_FORMAT_VERSION;
        int32_t  chunkX = 0;
        int32_t  chunkZ = 0;
        uint8_t  state = 0;
        uint8_t  flags = 0;
        uint16_t payloadVersion = 0;
        uint32_t payloadSize = 0;
    };

    inline void writeRegionHeader(BinaryWriter& w, const KcrHeader& h) {
        w.magic(REGION_MAGIC, 4);
        w.u16(h.version);
        w.i32(h.regionX);
        w.i32(h.regionZ);
        w.u32(h.chunkCount);
        w.u32(0); w.u32(0); w.u32(0);   // reserved[3]
        w.u16(0);                       // reserved pad -> 32 bytes
    }

    inline bool readRegionHeader(BinaryReader& r, KcrHeader& h) {
        char magic[4];
        if (!r.bytes(magic, 4)) return false;
        if (std::memcmp(magic, REGION_MAGIC, 4) != 0) return false;
        if (!r.u16(h.version) || h.version != REGION_FORMAT_VERSION) return false;
        if (!r.i32(h.regionX)) return false;
        if (!r.i32(h.regionZ)) return false;
        if (!r.u32(h.chunkCount)) return false;
        uint32_t tmp = 0;
        for (int i = 0; i < 3; ++i) if (!r.u32(tmp)) return false;
        uint16_t pad = 0;
        if (!r.u16(pad)) return false;
        return true;
    }

    // CRC32 (IEEE 802.3, reflected).
    inline uint32_t crc32(const void* data, std::size_t n) {
        const unsigned char* p = static_cast<const unsigned char*>(data);
        uint32_t crc = 0xFFFFFFFFu;
        for (std::size_t i = 0; i < n; ++i) {
            crc ^= p[i];
            for (int b = 0; b < 8; ++b)
                crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
        }
        return ~crc;
    }

    inline uint32_t dirEntryCrc(uint64_t offset, uint32_t size, uint32_t rawSize, uint8_t flags) {
        unsigned char buf[17];
        for (int i = 0; i < 8; ++i) buf[i]      = static_cast<unsigned char>((offset  >> (8 * i)) & 0xFF);
        for (int i = 0; i < 4; ++i) buf[8 + i]  = static_cast<unsigned char>((size    >> (8 * i)) & 0xFF);
        for (int i = 0; i < 4; ++i) buf[12 + i] = static_cast<unsigned char>((rawSize >> (8 * i)) & 0xFF);
        buf[16] = flags;
        return crc32(buf, sizeof(buf));
    }

    inline void writeDirEntry(BinaryWriter& w, const DirEntry& e) {
        w.u64(e.offset);
        w.u32(e.size);
        w.u32(e.rawSize);
        w.u8(e.flags);
        w.u32(dirEntryCrc(e.offset, e.size, e.rawSize, e.flags));
        for (int i = 0; i < 3; ++i) w.u8(0);   // reserved pad -> 24 bytes
    }

    // Reads one directory entry. crcOk is false when the entry carries a
    // checksum that does not match its bytes (torn/corrupt write); a stored
    // checksum of 0 means a legacy entry written before checksums existed and is
    // accepted after the caller's own bounds checks.
    inline bool readDirEntry(BinaryReader& r, DirEntry& e, bool& crcOk) {
        crcOk = true;
        if (!r.u64(e.offset)) return false;
        if (!r.u32(e.size)) return false;
        if (!r.u32(e.rawSize)) return false;
        if (!r.u8(e.flags)) return false;
        uint32_t stored = 0;
        if (!r.u32(stored)) return false;
        uint8_t tmp = 0;
        for (int i = 0; i < 3; ++i) if (!r.u8(tmp)) return false;
        if (stored != 0 && stored != dirEntryCrc(e.offset, e.size, e.rawSize, e.flags))
            crcOk = false;
        return true;
    }

    // Byte offset of KcrHeader::chunkCount (magic 4 + version 2 + regionX 4
    // + regionZ 4). Persist patches just this u32 instead of rewriting the
    // header, so a partial write can never damage magic/coords.
    inline constexpr std::size_t CHUNK_COUNT_OFFSET = 14;

    // Serialize a complete directory (every slot, index order) as one block so
    // a persist can patch all entries with a single positional write.
    inline std::vector<char> serializeDirectory(const DirEntry* entries, std::size_t count) {
        std::vector<char> out;
        out.reserve(count * DIR_ENTRY_SIZE);
        BinaryWriter w(out);
        for (std::size_t i = 0; i < count; ++i) writeDirEntry(w, entries[i]);
        return out;
    }

    // Build a complete chunk record (KCH header + text payload) from the bytes
    // produced by ChunkTemplate::serialize.
    inline std::vector<char> buildChunkRecord(int32_t gx, int32_t gz, uint8_t state,
                                              uint16_t payloadVersion,
                                              const std::vector<char>& payload) {
        std::vector<char> out;
        out.reserve(CHUNK_HEADER_SIZE + payload.size());
        BinaryWriter w(out);
        w.magic(CHUNK_MAGIC, 4);
        w.u16(CHUNK_FORMAT_VERSION);
        w.i32(gx);
        w.i32(gz);
        w.u8(state);
        w.u8(0);                                   // chunk flags
        w.u16(payloadVersion);
        w.u32(static_cast<uint32_t>(payload.size()));
        w.bytes(payload.data(), payload.size());
        return out;
    }

    // Validate a stored record and copy its payload out. The chunk coordinates
    // are duplicated in the record header on purpose: a directory/payload
    // disagreement is corruption and is rejected here.
    inline bool extractChunkPayload(const char* record, std::size_t recordSize,
                                    int32_t expectX, int32_t expectZ,
                                    std::vector<char>& outPayload) {
        if (!record || recordSize < CHUNK_HEADER_SIZE) return false;

        BinaryReader r(record, recordSize);
        char magic[4];
        KchHeader h;
        if (!r.bytes(magic, 4)) return false;
        if (std::memcmp(magic, CHUNK_MAGIC, 4) != 0) return false;
        if (!r.u16(h.version) || h.version != CHUNK_FORMAT_VERSION) return false;
        if (!r.i32(h.chunkX) || !r.i32(h.chunkZ)) return false;
        if (!r.u8(h.state)) return false;
        if (!r.u8(h.flags)) return false;
        if (!r.u16(h.payloadVersion)) return false;
        if (!r.u32(h.payloadSize)) return false;

        if (h.chunkX != expectX || h.chunkZ != expectZ) return false;
        if (h.payloadSize > r.remaining()) return false;
        // A record is exactly its header plus its declared payload. Any trailing
        // bytes mean the directory size and the record disagree: reject rather
        // than silently hand back a truncated/short payload.
        if (CHUNK_HEADER_SIZE + static_cast<std::size_t>(h.payloadSize) != recordSize)
            return false;

        outPayload.assign(record + r.position(), record + r.position() + h.payloadSize);
        return true;
    }
} // namespace kc::kcr
