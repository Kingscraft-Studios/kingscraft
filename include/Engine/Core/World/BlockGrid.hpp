#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include "Core/World/Location.hpp"

namespace kc {

    // Hybrid BlockGrid: dense array in memory for O(1) access + cache locality.
    // Air (0) is the default and should NEVER be treated as a "special sparse entry".
    // Serialization format remains unchanged in Phase 1.

    class BlockGrid {
    public:
        BlockGrid() = default;
        BlockGrid(int chunkSize, int height);

        BlockGrid(const BlockGrid&) = default;
        BlockGrid(BlockGrid&&) = default;
        BlockGrid& operator=(const BlockGrid&) = default;
        BlockGrid& operator=(BlockGrid&&) = default;

        // Map Like API
        uint64_t get(const Location& loc) const;
        void set(const Location& loc, uint64_t id);
        bool inBounds(const Location& loc) const;

        // Dimensions
        int getChunkSize() const { return chunkSize; }
        int getHeight() const { return height; }
        size_t getVolume() const;

        // Hot-path access (meshing, heightmaps, occupancy). Keep using this where tight loops exist.
        const std::vector<uint64_t>& getDenseData() const { return dense_; }
        std::vector<uint64_t>& getDenseData() { return dense_; }

        // COW
        std::shared_ptr<BlockGrid> clone() const;

        // Migration helpers (only for Phase 1 boundaries)
        static std::shared_ptr<BlockGrid> fromVector(const std::vector<uint64_t>& data, int chunkSize, int height);
        std::vector<uint64_t> toVector() const;

        void fillAir();

    private:
        int chunkSize = 0;
        int height = 0;

        // Row-major: index = y * chunkSize * chunkSize + z * chunkSize + x
        std::vector<uint64_t> dense_;
    };

    using BlockGridPtr = std::shared_ptr<BlockGrid>;
    using ConstBlockGridPtr = std::shared_ptr<const BlockGrid>;
}
