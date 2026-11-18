#include "Core/World/BlockGrid.hpp"

#include <algorithm>

namespace kc {

    BlockGrid::BlockGrid(int chunkSize, int height)
        : chunkSize(chunkSize), height(height) {
        if (chunkSize > 0 && height > 0) {
            dense_.assign(static_cast<size_t>(chunkSize) * static_cast<size_t>(height) * static_cast<size_t>(chunkSize), 0ULL);
        } else {
            dense_.clear();
        }
    }

    bool BlockGrid::inBounds(const Location& loc) const {
        if (chunkSize <= 0 || height <= 0) return false;
        return loc.x >= 0 && loc.x < chunkSize && loc.y >= 0 && loc.y < height && loc.z >= 0 && loc.z < chunkSize;
    }

    uint64_t BlockGrid::get(const Location& loc) const {
        if (!inBounds(loc)) return 0ULL;
        const size_t idx = static_cast<size_t>(loc.y) * static_cast<size_t>(chunkSize) * static_cast<size_t>(chunkSize) +
                           static_cast<size_t>(loc.z) * static_cast<size_t>(chunkSize) +
                           static_cast<size_t>(loc.x);
        if (idx >= dense_.size()) return 0ULL;
        return dense_[idx];
    }

    void BlockGrid::set(const Location& loc, uint64_t id) {
        if (!inBounds(loc)) return;
        const size_t idx = static_cast<size_t>(loc.y) * static_cast<size_t>(chunkSize) * static_cast<size_t>(chunkSize) +
                           static_cast<size_t>(loc.z) * static_cast<size_t>(chunkSize) +
                           static_cast<size_t>(loc.x);
        if (idx >= dense_.size()) return;
        dense_[idx] = id;
    }

    size_t BlockGrid::getVolume() const {
        if (chunkSize <= 0 || height <= 0) return 0;
        return static_cast<size_t>(chunkSize) * static_cast<size_t>(height) * static_cast<size_t>(chunkSize);
    }

    std::shared_ptr<BlockGrid> BlockGrid::clone() const {
        auto cloned = std::make_shared<BlockGrid>();
        cloned->chunkSize = chunkSize;
        cloned->height = height;
        cloned->dense_ = dense_;
        return cloned;
    }

    std::shared_ptr<BlockGrid> BlockGrid::fromVector(const std::vector<uint64_t>& data, int chunkSize, int height) {
        auto grid = std::make_shared<BlockGrid>();
        grid->chunkSize = chunkSize;
        grid->height = height;
        const size_t expected = (chunkSize > 0 && height > 0) ? (static_cast<size_t>(chunkSize) * static_cast<size_t>(height) * static_cast<size_t>(chunkSize)) : 0;
        if (expected == 0) {
            grid->dense_.clear();
            return grid;
        }
        if (data.size() == expected) {
            grid->dense_ = data;
        } else if (data.size() < expected) {
            grid->dense_ = data;
            grid->dense_.resize(expected, 0ULL);
        } else {
            grid->dense_.assign(data.begin(), data.begin() + expected);
        }
        return grid;
    }

    std::vector<uint64_t> BlockGrid::toVector() const {
        return dense_;
    }

    void BlockGrid::fillAir() {
        if (!dense_.empty()) {
            std::fill(dense_.begin(), dense_.end(), 0ULL);
        } else if (chunkSize > 0 && height > 0) {
            dense_.assign(static_cast<size_t>(chunkSize) * static_cast<size_t>(height) * static_cast<size_t>(chunkSize), 0ULL);
        }
    }

} // namespace kc
