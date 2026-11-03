#include "Core/World/SpawnFinder.hpp"

#include "Core/Blocks/Blocks.hpp"
#include "Core/World/Chunk.hpp"
#include "Core/World/Location.hpp"
#include "Core/World/World.hpp"
#include "Util/LogUtils.hpp"

#include <cmath>
#include <string>

namespace kc {

    namespace {
        // How far the spawn search will walk from the requested point looking
        // for land, counted in whole chunks rather than blocks. A chunk is the
        // natural unit here: each one is asked for its best column in a single
        // heightmap pass, so a wider ring costs a few more reads rather than a
        // few hundred more.
        constexpr int spawnSearchChunks = 14;

        // How many chunks one tick is allowed to inspect. The sweep is resumable,
        // so this only sets how smoothly the search spreads across frames; it
        // caps the per-tick cost either way.
        constexpr int spawnChunksPerTick = 64;

        // How much of the search area must have actually streamed in before the
        // sweep is allowed to conclude "this is all ocean" and settle for the
        // shallowest column. Without this the search can finish sweeping unloaded
        // chunks and drop the player on the seabed mid-load.
        constexpr int spawnMinInspectedChunks = 32;
    }

    void SpawnFinder::reset() {
        ring_ = 0;
        step_ = 0;
        inspected_ = 0;
        haveBest_ = false;
        bestSurface_ = 0;
        bestX_ = 0;
        bestZ_ = 0;
    }

    // The old code took surface + 1 at a fixed column. Water is not collidable
    // (CollisionSystem.cpp) and there is no swimming, so whenever that column
    // happened to be ocean the player was dropped onto the seabed, fully
    // submerged, with no way out -- and the depenetration pass fighting the fall
    // every tick read as the camera drifting on its own.
    //
    // The sweep works a whole chunk at a time: read that chunk's OCEAN_FLOOR
    // heightmap once, keep its single highest column, and only that one column
    // needs a block lookup to confirm it is dry. Rings come in nearest-first, so
    // the first chunk that yields dry ground wins, and the sweep stops there.
    SpawnFinder::Result SpawnFinder::poll(const World& world, int originX, int originZ) {
        // The cell encoding of water. Taken straight off the key rather than
        // resolved through the registry: Blocks::WATER.getEncoded() IS the value
        // WaterBlock::getEncodedId() would return, and the old
        // getShared(...)->getEncodedId() round trip cost an O(n) scan of the whole
        // registry under a lock on every search tick -- and returned "registry
        // not up yet", failing the spawn search outright whenever this ran before
        // block registration finished.
        const uint64_t waterId = Blocks::WATER.getEncoded();

        const int cs = world.getChunkSize();
        if (cs <= 0) return {};

        // Fast gate. Until the spawn chunk itself has streamed in there is
        // nothing to search outward from.
        const int originGx = static_cast<int>(std::floor(originX / static_cast<float>(cs)));
        const int originGz = static_cast<int>(std::floor(originZ / static_cast<float>(cs)));
        if (!world.getChunk(originGx, originGz)) return {};

        int budget = spawnChunksPerTick;
        while (budget > 0) {
            // Where we are on the square spiral. Ring 0 is the spawn chunk
            // itself; each later ring is walked one chunk at a time around its
            // perimeter.
            const int ring = ring_;
            if (ring > spawnSearchChunks) break;       // sweep finished

            int ringDx = 0, ringDz = 0;
            if (ring == 0) {
                // The spawn chunk. Advance the cursor so it is not re-scanned.
                ring_ = 1;
            } else {
                const int side = 2 * ring;
                const int perim = 4 * side;
                const int i = step_;
                if (i >= perim) { ring_ = ring + 1; step_ = 0; continue; }
                if      (i < side)       { ringDx = -ring + i;        ringDz = -ring; }
                else if (i < 2 * side)   { ringDx =  ring;             ringDz = -ring + (i - side); }
                else if (i < 3 * side)   { ringDx =  ring - (i - 2 * side); ringDz =  ring; }
                else                     { ringDx = -ring;             ringDz =  ring - (i - 3 * side); }
                ++step_;
            }
            --budget;

            const Chunk* chunk = world.getChunk(originGx + ringDx, originGz + ringDz);
            if (!chunk) continue;                       // still streaming in

            // One heightmap pass for the whole chunk. OCEAN_FLOOR, because
            // water is ocean fill rather than ground -- see World::getSurfaceHeight.
            const int n = chunk->getBlockDataSize();
            int bestLocalY = -1, bestLocalX = 0, bestLocalZ = 0;
            for (int lz = 0; lz < n; ++lz) {
                for (int lx = 0; lx < n; ++lx) {
                    const int top = chunk->getOceanFloorTopY(lx, lz);
                    if (top > bestLocalY) { bestLocalY = top; bestLocalX = lx; bestLocalZ = lz; }
                }
            }
            if (bestLocalY < 0) continue;              // nothing but air
            ++inspected_;

            const int surfaceY = chunk->localToWorldY(bestLocalY);
            if (surfaceY >= world.getMaxY() - 1) continue;

            // Remember the highest ground anywhere seen. If the whole swept area
            // turns out to be water this is the shallowest point in it, which is
            // a far kinder outcome than the bottom of the sea.
            if (!haveBest_ || surfaceY > bestSurface_) {
                haveBest_ = true;
                bestSurface_ = surfaceY;
                bestX_ = (originGx + ringDx) * cs + bestLocalX;
                bestZ_ = (originGz + ringDz) * cs + bestLocalZ;
            }

            // Feet and head must both be clear of liquid, checked on the actual
            // block so this stays correct if water placement changes.
            const int feetLocal = chunk->worldToLocalY(surfaceY + 1);
            const int headLocal = chunk->worldToLocalY(surfaceY + 2);
            const bool submerged =
                chunk->getBlock(Location(bestLocalX, feetLocal, bestLocalZ)) == waterId ||
                chunk->getBlock(Location(bestLocalX, headLocal, bestLocalZ)) == waterId;
            if (submerged) continue;

            // Nearest dry ground wins, so the player lands as close to the
            // requested point as the map allows.
            const int wx = (originGx + ringDx) * cs + bestLocalX;
            const int wz = (originGz + ringDz) * cs + bestLocalZ;

            reset();   // this sweep is finished; the next request starts fresh
            LogUtils::info(ThreadName::GameLogic,
                "spawn: placed on dry land at (" + std::to_string(wx) + ", " +
                std::to_string(surfaceY) + ", " + std::to_string(wz) + ")");

            Result result;
            result.status = Status::Placed;
            result.column = Column{wx, surfaceY, wz, true};
            return result;
        }

        // Swept the whole area without finding dry ground. Commit to the
        // shallowest column rather than leaving the player falling forever --
        // but only once enough of the area has actually loaded to justify
        // concluding it really is all ocean. Otherwise restart the sweep and let
        // more chunks stream in first.
        if (ring_ > spawnSearchChunks) {
            if (inspected_ < spawnMinInspectedChunks) {
                reset();
                return {};
            }
            if (haveBest_) {
                const int surfaceY = bestSurface_;
                const int wx = bestX_;
                const int wz = bestZ_;
                const int inspected = inspected_;
                reset();
                LogUtils::warn(ThreadName::GameLogic,
                    "spawn: no dry land within " + std::to_string(spawnSearchChunks) +
                    " chunks (" + std::to_string(inspected) +
                    " loaded chunks inspected); settling on the shallowest water at (" +
                    std::to_string(wx) + ", " + std::to_string(surfaceY) + ", " +
                    std::to_string(wz) + ")");

                Result result;
                result.status = Status::Placed;
                result.column = Column{wx, surfaceY, wz, false};
                return result;
            }
        }

        return {};
    }

} // namespace kc
