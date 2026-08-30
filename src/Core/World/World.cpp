#include "Core/World/World.hpp"
#include "Core/World/ChunkKey.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "Bus/MessageBus.hpp"
#include "Core/Blocks/Block.hpp"
#include "Core/World/Physics/Gravity.hpp"
#include "Renderer/RendererSettings.hpp"
#include "Threads/InputThread.hpp"
#include "Threads/IO.hpp"
#include "Threads/Renderer.hpp"
#include "Util/LogUtils.hpp"

namespace {

    std::vector<uint8_t> extractEdgeStrip(const std::vector<uint8_t>& src, int N, int h, int edgeDir) {
        std::vector<uint8_t> strip(static_cast<size_t>(h) * N, 0);
        if (edgeDir < 2) {
            int x = (edgeDir == 0) ? 0 : (N - 1);
            for (int y = 0; y < h; ++y)
                for (int z = 0; z < N; ++z)
                    strip[static_cast<size_t>(y) * N + z] = src[static_cast<size_t>(y) * N * N + z * N + x];
        } else {
            int z = (edgeDir == 2) ? 0 : (N - 1);
            for (int y = 0; y < h; ++y)
                for (int xc = 0; xc < N; ++xc)
                    strip[static_cast<size_t>(y) * N + xc] = src[static_cast<size_t>(y) * N * N + z * N + xc];
        }
        return strip;
    }

    // Remove boundary-plane faces pointing through the given gates from a
    // sub-chunk's mesh, then append the freshly emitted gate faces computed on
    // the gen thread. Interior faces whose direction happens to match a gate
    // (e.g. an +X face against an interior air pocket) are untouched — they
    // border the chunk's own blocks, not the neighbor.
    void spliceGateFaces(lve::SubChunk& dst, const lve::SubChunk& slab, uint8_t mask, int N) {
        const int edge = N;   // face plane at N or 0

        auto isGateFace = [mask, edge](const lve::ChunkVertex& v) -> bool {
            if (!(mask & (static_cast<uint8_t>(1) << v.face))) return false;
            switch (v.face) {
                case 4: return static_cast<int>(v.px) == edge;   // PosX
                case 5: return static_cast<int>(v.px) == 0;      // NegX
                case 2: return static_cast<int>(v.pz) == edge;   // PosZ
                default: return static_cast<int>(v.pz) == 0;     // NegZ
            }
        };

        std::vector<uint32_t> remap(dst.vertices.size());
        std::vector<lve::ChunkVertex> keep;
        keep.reserve(dst.vertices.size());

        uint32_t next = 0;
        for (size_t i = 0; i < dst.vertices.size(); ++i) {
            if (isGateFace(dst.vertices[i])) continue;
            remap[i] = next++;
            keep.push_back(dst.vertices[i]);
        }

        std::vector<uint16_t> indices;
        indices.reserve(dst.indices.size() + slab.indices.size());
        for (uint16_t idx : dst.indices) {
            if (isGateFace(dst.vertices[idx])) continue;
            indices.push_back(static_cast<uint16_t>(remap[idx]));
        }
        uint32_t base = next;
        for (uint16_t idx : slab.indices)
            indices.push_back(static_cast<uint16_t>(base + idx));

        dst.vertices = std::move(keep);
        dst.vertices.insert(dst.vertices.end(), slab.vertices.begin(), slab.vertices.end());
        dst.indices = std::move(indices);
        dst.indexCount = static_cast<uint32_t>(dst.indices.size());
    }

} // namespace

namespace lve {

    World::World(ITerrainGenerator& terrainGen, int chunkSize, int height)
        : terrainGen_(terrainGen), chunkSize_(chunkSize), height_(height) {
        noiseThread_ = std::thread([this]() { noiseThreadFunc(); });
        meshThread_ = std::thread([this]() { meshThreadFunc(); });
        playerController_.init(InputThread::getInstance().getKeyBindHandler());
    }

    World::~World() {
        noiseRunning_ = false;
        meshRunning_ = false;
        noiseCV_.notify_all();
        meshCV_.notify_all();
        if (meshThread_.joinable()) meshThread_.join();
        if (noiseThread_.joinable()) noiseThread_.join();

        // Save the overlay: only chunks whose blocks were edited this session.
        // Unedited chunks regenerate from the seed and need no disk copy. IO
        // flushes each changed region file once.
        {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            auto& regions = IO::Get().getBuiltinTemplates().getRegionTemplate();
            for (uint64_t key : unsavedChunks_) {
                auto cacheIt = blockCache_.find(key);
                if (cacheIt == blockCache_.end()) continue;
                int gx = static_cast<int>(key >> 32);
                int gz = static_cast<int>(key & 0xFFFFFFFF);
                regions.queueSave(gx, gz, *cacheIt->second);
            }
        }

        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            pendingGen_.clear();
            toMesh_.clear();
            completedGen_.clear();
            pendingRequests_.clear();
            remeshQueue_.clear();
            remeshGates_.clear();
            completedGateRemesh_.clear();
            editQueue_.clear();
            editMasks_.clear();
            completedEdits_.clear();
        }
        for (auto& bucket : pendingCleanup_) {
            bucket.clear();
        }
    }

    int World::worldToGrid(float worldCoord, int chunkSize) {
        return static_cast<int>(std::floor(worldCoord / chunkSize));
    }

    bool World::isChunkLoaded(int gridX, int gridZ) const {
        return chunks_.find(makeChunkKey(gridX, gridZ)) != chunks_.end();
    }

    Chunk* World::getChunk(int gridX, int gridZ) {
        auto it = chunks_.find(makeChunkKey(gridX, gridZ));
        return (it != chunks_.end()) ? it->second.get() : nullptr;
    }

    const Chunk* World::getChunk(int gridX, int gridZ) const {
        auto it = chunks_.find(makeChunkKey(gridX, gridZ));
        return (it != chunks_.end()) ? it->second.get() : nullptr;
    }

    bool World::setBlock(int worldX, int worldY, int worldZ, const Block& block) {
        if (worldY < 0 || worldY >= height_) return false;
        int gx = worldToGrid(static_cast<float>(worldX), chunkSize_);
        int gz = worldToGrid(static_cast<float>(worldZ), chunkSize_);
        Chunk* chunk = getChunk(gx, gz);
        if (!chunk) return false;
        int lx = worldX - gx * chunkSize_;
        int lz = worldZ - gz * chunkSize_;
        chunk->setBlock(lx, worldY, lz, static_cast<uint8_t>(block.getId()));

        // chunk->setBlock copy-on-writes to a fresh buffer; point the cache at
        // the same handle so cache and live chunk always share one grid.
        {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            blockCache_[makeChunkKey(gx, gz)] = chunk->getBlockDataPtr();
            unsavedChunks_.insert(makeChunkKey(gx, gz));
        }

        if (lx == 0) queueGateRemesh(gx - 1, gz, 4);              // -X neighbor: re-cut its +X face
        else if (lx == chunkSize_ - 1) queueGateRemesh(gx + 1, gz, 5);   // +X neighbor: re-cut its -X face
        if (lz == 0) queueGateRemesh(gx, gz - 1, 2);                // -Z neighbor: re-cut its +Z face
        else if (lz == chunkSize_ - 1) queueGateRemesh(gx, gz + 1, 3); // +Z neighbor: re-cut its -Z face

        return true;
    }

    uint8_t World::getBlock(int worldX, int worldY, int worldZ) const {
        if (worldY < 0 || worldY >= height_) return 0;
        int gx = worldToGrid(static_cast<float>(worldX), chunkSize_);
        int gz = worldToGrid(static_cast<float>(worldZ), chunkSize_);
        auto it = chunks_.find(makeChunkKey(gx, gz));
        if (it == chunks_.end()) return 0;
        int lx = worldX - gx * chunkSize_;
        int lz = worldZ - gz * chunkSize_;
        return it->second->getBlock(lx, worldY, lz);
    }

    int World::getSurfaceHeight(int worldX, int worldZ) const {
        int gx = worldToGrid(static_cast<float>(worldX), chunkSize_);
        int gz = worldToGrid(static_cast<float>(worldZ), chunkSize_);
        const Chunk* chunk = getChunk(gx, gz);
        if (!chunk) return -1;
        int lx = worldX - gx * chunkSize_;
        int lz = worldZ - gz * chunkSize_;
        return chunk->getHeightAt(lx, lz);
    }

    void World::remeshDirtyChunks() {
        // Block edits are now rebuilt on the mesher thread: collect the
        // affected subchunks per dirty chunk and hand the mesher a task. The
        // heavy geometry work no longer runs on the game thread. Coalesces
        // repeated edits to the same chunk before the mesher picks it up.
        std::lock_guard<std::mutex> lock(queueMutex_);
        for (auto& [key, chunk] : chunks_) {
            if (!chunk->isRemeshNeeded()) continue;

            uint32_t mask = 0;
            uint32_t i = 0;
            for (auto& sub : chunk->getSubChunks()) {
                if (sub.meshNeeded) mask |= (1u << i);
                ++i;
            }
            if (mask == 0) continue;

            auto it = editMasks_.find(key);
            if (it != editMasks_.end()) {
                it->second |= mask;   // already queued for this chunk: accumulate
                continue;
            }
            editMasks_[key] = mask;
            editQueue_.push_back(key);
            meshCV_.notify_one();
        }
    }

    void World::loadChunkSync(int gridX, int gridZ) {
        if (isChunkLoaded(gridX, gridZ)) return;

        int N = chunkSize_;
        int h = height_;

        // TODO: Using IO Sequentially isn't a great idea
        auto blockIds = IO::Get().getBuiltinTemplates().getRegionTemplate().load(gridX, gridZ);
        if (blockIds.empty()) {
            // Not on disk (never edited) -> deterministic regeneration from the
            // hardcoded seed. Generated chunks are NOT written back; only block
            // edits reach the region overlay, so a chunk's presence on disk is
            // exactly "this chunk differs from what the seed would produce".
            blockIds = terrainGen_.generateBlocks(gridX, gridZ, N, h);
        }

        BlockDataPtr handle;
        {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            handle = std::make_shared<const std::vector<uint8_t>>(std::move(blockIds));
            blockCache_[makeChunkKey(gridX, gridZ)] = handle;
        }

        std::vector<uint8_t> edgePosX, edgeNegX, edgePosZ, edgeNegZ;
        const std::vector<uint8_t>* pEdgePosX = nullptr;
        const std::vector<uint8_t>* pEdgeNegX = nullptr;
        const std::vector<uint8_t>* pEdgePosZ = nullptr;
        const std::vector<uint8_t>* pEdgeNegZ = nullptr;
        {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            auto it = blockCache_.find(makeChunkKey(gridX + 1, gridZ));
            if (it != blockCache_.end()) { edgePosX = extractEdgeStrip(*it->second, N, h, 0); pEdgePosX = &edgePosX; }
            it = blockCache_.find(makeChunkKey(gridX - 1, gridZ));
            if (it != blockCache_.end()) { edgeNegX = extractEdgeStrip(*it->second, N, h, 1); pEdgeNegX = &edgeNegX; }
            it = blockCache_.find(makeChunkKey(gridX, gridZ + 1));
            if (it != blockCache_.end()) { edgePosZ = extractEdgeStrip(*it->second, N, h, 2); pEdgePosZ = &edgePosZ; }
            it = blockCache_.find(makeChunkKey(gridX, gridZ - 1));
            if (it != blockCache_.end()) { edgeNegZ = extractEdgeStrip(*it->second, N, h, 3); pEdgeNegZ = &edgeNegZ; }
        }

        auto chunk = std::make_unique<Chunk>(glm::ivec2(gridX, gridZ), N, 1.0f, h);
        chunk->setBlockData(handle, N, h);           // shares one grid with blockCache_
        for (auto& sub : chunk->getSubChunks()) {
            ChunkMesher::generateSubChunk(sub, chunk->getBlockData(), N, h, sub.yBase,
                                           pEdgePosX, pEdgeNegX, pEdgePosZ, pEdgeNegZ);
        }
        chunk->upload();

        chunks_[makeChunkKey(gridX, gridZ)] = std::move(chunk);
        chunkCacheDirty_ = true;
    }

    void World::unloadChunk(int gridX, int gridZ) {
        uint64_t key = makeChunkKey(gridX, gridZ);  // compute once

        // Stage the chunk for saving ONLY if its blocks were edited since load.
        // Unedited chunks regenerate from the seed and never touch disk, so a
        // chunk present in a region file always means "differs from seed".
        {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            auto cacheIt = blockCache_.find(key);
            if (cacheIt != blockCache_.end() && unsavedChunks_.count(key)) {
                IO::Get().getBuiltinTemplates().getRegionTemplate().queueSave(gridX, gridZ, *cacheIt->second);
            }
            unsavedChunks_.erase(key);
        }

        auto it = chunks_.find(key);
        if (it == chunks_.end()) return;

        int idx = static_cast<int>(frameCount_ % pendingCleanup_.size());
        pendingCleanup_[idx].push_back(std::move(it->second));
        chunks_.erase(it);

        // NEW: Signal render thread to defer GPU cleanup
        MessageBus::Get().send(ThreadName::Renderer, [key]() {
            RenderThread::getInstance().getUploader().unload(key);
        });

        {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            blockCache_.erase(key);
        }
        chunkCacheDirty_ = true;

        // Neighbors lose this chunk's opaque border — mark them so their
        // faces toward it are re-emitted on the gen thread.
        markNeighborsForBorder(gridX, gridZ);
    }

    void World::flushPendingCleanup() {
        if (frameCount_ >= CLEANUP_DELAY) {
            int clearIdx = static_cast<int>((frameCount_ - CLEANUP_DELAY) % pendingCleanup_.size());
            pendingCleanup_[clearIdx].clear();
        }
        ++frameCount_;
    }

    // Noise thread: block resolution only. Disk IO / noise generation fills
    // blockCache_, then the chunk key is handed to the mesher thread.
    void World::noiseThreadFunc() {
        while (noiseRunning_) {
            std::pair<int, int> task;
            {
                std::unique_lock<std::mutex> lock(queueMutex_);
                if (!noiseCV_.wait_for(lock, std::chrono::milliseconds(50),
                    [this]() { return !pendingGen_.empty() || !noiseRunning_; })) {
                    continue;
                }
                if (!noiseRunning_) break;
                task = pendingGen_.front();
                pendingGen_.pop_front();
            }

            int gx = task.first;
            int gz = task.second;

            // TODO: Using IO Sequentially isn't a great idea
            auto blockIds = IO::Get().getBuiltinTemplates().getRegionTemplate().load(gx, gz);
            if (blockIds.empty()) {
                // Deterministic regeneration from the hardcoded seed. Not
                // written back to disk — a chunk on disk means it was edited.
                blockIds = terrainGen_.generateBlocks(gx, gz, chunkSize_, height_);
            }

            {
                std::lock_guard<std::mutex> lock(cacheMutex_);
                blockCache_[makeChunkKey(gx, gz)] =
                    std::make_shared<const std::vector<uint8_t>>(std::move(blockIds));
            }

            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                toMesh_.push_back(makeChunkKey(gx, gz));
            }
            meshCV_.notify_one();
        }
    }

    // Mesher thread: owns every meshing task. User block-edit rebuilds first
    // (only 1-3 subchunks, must feel instant — a streaming backlog must not
    // delay them), then fresh streaming frontier chunks, then border re-culls
    // (lowest priority so seams get re-cut when idle).
    void World::meshThreadFunc() {
        int N = chunkSize_;
        int h = height_;

        while (meshRunning_) {
            bool isGate = false;
            bool isEdit = false;
            uint64_t key = 0;
            {
                std::unique_lock<std::mutex> lock(queueMutex_);
                if (!meshCV_.wait_for(lock, std::chrono::milliseconds(50),
                    [this]() { return !toMesh_.empty() || !editQueue_.empty() || !remeshQueue_.empty() || !meshRunning_; })) {
                    continue;
                }
                if (!meshRunning_) break;
                if (!editQueue_.empty()) {           // user block edits first (tiny, must be instant)
                    key = editQueue_.front();
                    editQueue_.pop_front();
                    isEdit = true;
                } else if (!toMesh_.empty()) {       // then fresh streaming meshes
                    key = toMesh_.front();
                    toMesh_.pop_front();
                } else if (!remeshQueue_.empty()) {  // gate re-culls when idle
                    key = remeshQueue_.front();
                    remeshQueue_.pop_front();
                    isGate = true;
                } else {
                    continue;                        // spurious wake
                }
            }

            int gx = static_cast<int>(key >> 32);
            int gz = static_cast<int>(key & 0xFFFFFFFF);

            if (isEdit) {
                uint32_t mask = 0;
                {
                    std::lock_guard<std::mutex> lock(queueMutex_);
                    auto it = editMasks_.find(key);
                    if (it == editMasks_.end()) continue;  // cancelled by unload cleanup
                    mask = it->second;
                    editMasks_.erase(it);
                }

                BlockDataPtr blockData;
                {
                    std::lock_guard<std::mutex> lock(cacheMutex_);
                    auto it = blockCache_.find(key);
                    if (it == blockCache_.end()) continue;  // chunk unloaded before it ran
                    blockData = it->second;
                }

                std::vector<uint8_t> strip[4];
                const std::vector<uint8_t>* edges[4] = {};
                {
                    std::lock_guard<std::mutex> lock(cacheMutex_);
                    constexpr int edx[4] = {1, -1, 0, 0};
                    constexpr int edz[4] = {0, 0, 1, -1};
                    for (int e = 0; e < 4; ++e) {
                        auto it = blockCache_.find(makeChunkKey(gx + edx[e], gz + edz[e]));
                        if (it != blockCache_.end()) {
                            strip[e] = extractEdgeStrip(*it->second, N, h, e);
                            edges[e] = &strip[e];
                        }
                    }
                }

                int subCount = (h + static_cast<int>(SUBCHUNK_H) - 1) / static_cast<int>(SUBCHUNK_H);
                ChunkEditResult result;
                result.gx = gx;
                result.gz = gz;
                result.mask = mask;
                result.subs.reserve(static_cast<size_t>(subCount));
                for (int i = 0; i < subCount; ++i) {
                    if (!(mask & (1u << static_cast<uint32_t>(i)))) continue;
                    SubChunk sub{};
                    sub.yBase = i * static_cast<int>(SUBCHUNK_H);
                    ChunkMesher::generateSubChunk(sub, *blockData, N, h, sub.yBase,
                                                  edges[0], edges[1], edges[2], edges[3]);
                    sub.indexCount = static_cast<uint32_t>(sub.indices.size());
                    result.subs.push_back(std::move(sub));
                }

                {
                    std::lock_guard<std::mutex> lock(queueMutex_);
                    completedEdits_.push_back(std::move(result));
                }
                continue;
            }

            if (isGate) {
                uint8_t mask = 0;
                {
                    std::lock_guard<std::mutex> lock(queueMutex_);
                    auto it = remeshGates_.find(key);
                    if (it == remeshGates_.end()) continue;  // cancelled by unload cleanup
                    mask = it->second;
                    remeshGates_.erase(it);
                }

                BlockDataPtr blockData;
                {
                    std::lock_guard<std::mutex> lock(cacheMutex_);
                    auto it = blockCache_.find(key);
                    if (it == blockCache_.end()) continue;  // chunk unloaded before it ran
                    blockData = it->second;
                }

                // Only re-emit faces looking through the changed gate(s): one
                // boundary plane per sub-chunk, instead of re-meshing all 25.
                int subCount = (h + static_cast<int>(SUBCHUNK_H) - 1) / static_cast<int>(SUBCHUNK_H);
                GateRemeshResult result;
                result.gx = gx;
                result.gz = gz;
                result.mask = mask;
                result.slabs.reserve(static_cast<size_t>(subCount));

                // gate -> neighbor direction -> mesher edge strip index:
                //   PosX(4) -> +X neighbor          -> edgePosX (dir 0)
                //   NegX(5) -> -X neighbor          -> edgeNegX (dir 1)
                //   PosZ(2) -> +Z neighbor          -> edgePosZ (dir 2)
                //   NegZ(3) -> -Z neighbor          -> edgeNegZ (dir 3)
                for (int si = 0; si < subCount; ++si) {
                    SubChunk slab{};
                    slab.yBase = si * static_cast<int>(SUBCHUNK_H);
                    for (int gate = 2; gate <= 5; ++gate) {
                        if (!(mask & (static_cast<uint8_t>(1) << gate))) continue;

                        constexpr int gateStripDir[6] = {-1, -1, 2, 3, 0, 1};
                        int sdir = gateStripDir[gate];
                        int nbx = gx;
                        int nbz = gz;
                        if (sdir == 0) nbx += 1;
                        else if (sdir == 1) nbx -= 1;
                        else if (sdir == 2) nbz += 1;
                        else nbz -= 1;

                        std::vector<uint8_t> strip;
                        const std::vector<uint8_t>* pStrip = nullptr;
                        {
                            std::lock_guard<std::mutex> lock(cacheMutex_);
                            auto it = blockCache_.find(makeChunkKey(nbx, nbz));
                            if (it != blockCache_.end()) {
                                strip = extractEdgeStrip(*it->second, N, h, sdir);
                                pStrip = &strip;
                            }
                        }

                        const std::vector<uint8_t>* edges[4] = {};
                        edges[sdir] = pStrip;
                        ChunkMesher::emitGateFaces(slab, *blockData, N, h, slab.yBase, gate,
                                                   edges[0], edges[1], edges[2], edges[3]);
                    }
                    slab.indexCount = static_cast<uint32_t>(slab.indices.size());
                    result.slabs.push_back(std::move(slab));
                }

                {
                    std::lock_guard<std::mutex> lock(queueMutex_);
                    completedGateRemesh_.push_back(std::move(result));
                }
                continue;
            }

            // Fresh chunk mesh.
            BlockDataPtr blockData;
            {
                std::lock_guard<std::mutex> lock(cacheMutex_);
                auto it = blockCache_.find(key);
                if (it == blockCache_.end()) continue;   // unloaded before meshing — drop
                blockData = it->second;
            }

            std::vector<uint8_t> strip[4];
            const std::vector<uint8_t>* edges[4] = {};
            {
                std::lock_guard<std::mutex> lock(cacheMutex_);
                constexpr int edx[4] = {1, -1, 0, 0};
                constexpr int edz[4] = {0, 0, 1, -1};
                for (int e = 0; e < 4; ++e) {
                    auto it = blockCache_.find(makeChunkKey(gx + edx[e], gz + edz[e]));
                    if (it != blockCache_.end()) {
                        strip[e] = extractEdgeStrip(*it->second, N, h, e);
                        edges[e] = &strip[e];
                    }
                }
            }

            auto tempChunk = std::make_unique<Chunk>(glm::ivec2(gx, gz), N, 1.0f, h);
            tempChunk->setBlockData(blockData, N, h);   // shares the cache grid, no copy
            const std::vector<uint8_t>& chunkData = tempChunk->getBlockData();
            for (auto& sub : tempChunk->getSubChunks()) {
                ChunkMesher::generateSubChunk(sub, chunkData, N, h, sub.yBase,
                                              edges[0], edges[1], edges[2], edges[3]);
            }

            ChunkGenResult result;
            result.gx = gx;
            result.gz = gz;
            result.chunk = std::move(tempChunk);
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                completedGen_.push_back(std::move(result));
            }
        }
    }

    void World::processCompletedChunks() {
        std::deque<ChunkGenResult> results;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            results.swap(completedGen_);
        }

        for (auto& r : results) {
            uint64_t key = makeChunkKey(r.gx, r.gz);
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                pendingRequests_.erase(key);
            }

            if (chunks_.count(key)) continue;

            r.chunk->upload();
            chunks_[key] = std::move(r.chunk);
            chunkCacheDirty_ = true;

            // Border faces of already-loaded neighbors must be re-culled now
            // that this chunk exists. One-way only: we never mark during a
            // re-mesh, so this cannot cascade.
            markNeighborsForBorder(r.gx, r.gz);
        }
    }

    void World::markNeighborsForBorder(int gx, int gz) {
        // The chunk at (gx,gz) appeared or disappeared. Each of its loaded
        // neighbors only needs its faces POINTING INTO that chunk re-evaluated,
        // not a full re-mesh. gate = the neighbor's face direction toward (gx,gz).
        constexpr int ndx[4] = {1, -1, 0, 0};
        constexpr int ndz[4] = {0, 0, 1, -1};
        constexpr int nde[4] = {5, 4, 3, 2};   // -X,+X,-Z,+Z faces toward (gx,gz)
        for (int i = 0; i < 4; ++i) {
            int nx = gx + ndx[i];
            int nz = gz + ndz[i];
            if (!isChunkLoaded(nx, nz)) continue;
            queueGateRemesh(nx, nz, nde[i]);
        }
    }

    void World::queueGateRemesh(int gx, int gz, int gate) {
        if (remeshRequestsThisTick_ >= MAX_REMESH_PER_FRAME) return;
        uint64_t key = makeChunkKey(gx, gz);
        uint8_t bit = static_cast<uint8_t>(1u << gate);
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (pendingRequests_.count(key)) return;  // not meshed yet; full build will cover it
            auto it = remeshGates_.find(key);
            if (it != remeshGates_.end()) {
                it->second |= bit;                    // already queued -> accumulate gate
                return;
            }
            remeshGates_[key] = bit;
            remeshQueue_.push_back(key);
        }
        ++remeshRequestsThisTick_;
        meshCV_.notify_one();   // gate re-culls are mesher-thread work
    }

    void World::processGateRemeshResults() {
        std::deque<GateRemeshResult> results;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            results.swap(completedGateRemesh_);
        }

        for (auto& r : results) {
            auto it = chunks_.find(makeChunkKey(r.gx, r.gz));
            if (it == chunks_.end()) continue;   // unloaded before the re-mesh finished

            Chunk* existing = it->second.get();
            auto& dst = existing->getSubChunks();
            auto& src = r.slabs;
            if (dst.size() != src.size()) continue;

            for (size_t i = 0; i < dst.size(); ++i) {
                spliceGateFaces(dst[i], src[i], r.mask, chunkSize_);
            }

            // Upload skips GPU work when the merged geometry hash is unchanged.
            existing->upload();
        }
    }

    void World::processEditResults() {
        std::deque<ChunkEditResult> results;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            results.swap(completedEdits_);
        }

        for (auto& r : results) {
            auto it = chunks_.find(makeChunkKey(r.gx, r.gz));
            if (it == chunks_.end()) continue;   // unloaded before the rebuild finished

            Chunk* existing = it->second.get();
            auto& dst = existing->getSubChunks();
            size_t k = 0;
            for (size_t i = 0; i < dst.size() && k < r.subs.size(); ++i) {
                if (r.mask & (1u << static_cast<uint32_t>(i))) {
                    dst[i] = std::move(r.subs[k++]);
                }
            }

            // Upload skips GPU work when the merged geometry hash is unchanged.
            existing->upload();
        }
    }

    void World::tick(double dt) {
        remeshRequestsThisTick_ = 0;

        float cameraX = playerController_.getCamera().getPosition().x;
        float cameraZ = playerController_.getCamera().getPosition().z;
        int renderDistance = RendererSettings::get().renderDistance;

        int centerX = worldToGrid(cameraX, chunkSize_);
        int centerZ = worldToGrid(cameraZ, chunkSize_);

        // Unload chunks outside render distance
        std::vector<uint64_t> toUnload;
        for (auto& [key, chunk] : chunks_) {
            int gx = static_cast<int>(key >> 32);
            int gz = static_cast<int>(key & 0xFFFFFFFF);
            if (std::abs(gx - centerX) > renderDistance ||
                std::abs(gz - centerZ) > renderDistance) {
                toUnload.push_back(key);
            }
        }
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            for (uint64_t key : toUnload) {
                pendingRequests_.erase(key);
                remeshGates_.erase(key);
                editMasks_.erase(key);
                // Drop stale queued work instead of generating a chunk that is
                // immediately unloaded again.
                for (auto it = pendingGen_.begin(); it != pendingGen_.end();) {
                    if (makeChunkKey(it->first, it->second) == key) it = pendingGen_.erase(it);
                    else ++it;
                }
                for (auto it = remeshQueue_.begin(); it != remeshQueue_.end();) {
                    if (*it == key) it = remeshQueue_.erase(it);
                    else ++it;
                }
                for (auto it = editQueue_.begin(); it != editQueue_.end();) {
                    if (*it == key) it = editQueue_.erase(it);
                    else ++it;
                }
            }
        }
        for (uint64_t key : toUnload) {
            int gx = static_cast<int>(key >> 32);
            int gz = static_cast<int>(key & 0xFFFFFFFF);
            unloadChunk(gx, gz);
        }

        // Load center 3x3 chunks synchronously on first update
        if (chunks_.empty()) {
            for (int gz = centerZ - HIGH_PRIO_RADIUS; gz <= centerZ + HIGH_PRIO_RADIUS; ++gz) {
                for (int gx = centerX - HIGH_PRIO_RADIUS; gx <= centerX + HIGH_PRIO_RADIUS; ++gx) {
                    loadChunkSync(gx, gz);
                }
            }
            hasCenter_ = true;
            lastCenterX_ = centerX;
            lastCenterZ_ = centerZ;
        }
        // Keep the camera chunk solid: re-sync it when the player crosses a
        // chunk boundary, otherwise the async ring may not have built it yet
        // and the player falls through the world.
        else if (centerX != lastCenterX_ || centerZ != lastCenterZ_) {
            if (!isChunkLoaded(centerX, centerZ)) {
                loadChunkSync(centerX, centerZ);
            }
            lastCenterX_ = centerX;
            lastCenterZ_ = centerZ;
        }

        // Queue async loads in expanding rings (closest to player first)
        int queued = 0;

        // Track a smoothed horizontal movement direction from the camera delta
        // between ticks. Used below to prefer chunks ahead of the player.
        float moveX = cameraX - prevCameraX_;
        float moveZ = cameraZ - prevCameraZ_;
        prevCameraX_ = cameraX;
        prevCameraZ_ = cameraZ;
        float moveLen = std::sqrt(moveX * moveX + moveZ * moveZ);
        if (moveLen > 0.001f) {
            float wx = moveX / moveLen;
            float wz = moveZ / moveLen;
            if (hasMoveDir_) {
                float blend = 0.4f;
                moveDirX_ = moveDirX_ * (1.0f - blend) + wx * blend;
                moveDirZ_ = moveDirZ_ * (1.0f - blend) + wz * blend;
                float ml = std::sqrt(moveDirX_ * moveDirX_ + moveDirZ_ * moveDirZ_);
                if (ml > 1e-6f) {
                    moveDirX_ /= ml;
                    moveDirZ_ /= ml;
                }
            } else {
                moveDirX_ = wx;
                moveDirZ_ = wz;
                hasMoveDir_ = true;
            }
        }
        const float dirX = moveDirX_;
        const float dirZ = moveDirZ_;

        // Horizontal camera look direction. Unlike the movement EMA this is
        // meaningful while standing still and spinning, so generation follows
        // what's actually on screen.
        float viewX = playerController_.getCamera().getForward().x;
        float viewZ = playerController_.getCamera().getForward().z;
        float viewLen = std::sqrt(viewX * viewX + viewZ * viewZ);
        if (viewLen > 1e-4f) {
            viewX /= viewLen;
            viewZ /= viewLen;
        } else {                    // pitched straight up/down: fall back to heading
            viewX = dirX;
            viewZ = dirZ;
        }

        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            for (int r = HIGH_PRIO_RADIUS + 1; r <= renderDistance; ++r) {
                auto tryQueue = [&](int gx, int gz) {
                    uint64_t key = makeChunkKey(gx, gz);
                    if (chunks_.count(key)) return;
                    if (pendingRequests_.count(key)) return;
                    if (queued >= MAX_REQUESTS_PER_FRAME) return;

                    pendingGen_.push_back({gx, gz});
                    pendingRequests_.insert(key);
                    ++queued;
                };

                // top edge: (centerZ - r)
                for (int gx = centerX - r; gx <= centerX + r; ++gx)
                    tryQueue(gx, centerZ - r);

                // bottom edge: (centerZ + r)
                for (int gx = centerX - r; gx <= centerX + r; ++gx)
                    tryQueue(gx, centerZ + r);

                // left edge: (centerX - r), skip corners
                for (int gz = centerZ - r + 1; gz <= centerZ + r - 1; ++gz)
                    tryQueue(centerX - r, gz);

                // right edge: (centerX + r), skip corners
                for (int gz = centerZ - r + 1; gz <= centerZ + r - 1; ++gz)
                    tryQueue(centerX + r, gz);

                if (queued >= MAX_REQUESTS_PER_FRAME) break;
            }

            // Re-prioritize against the CURRENT center + camera: while the
            // player moves, earlier-enqueued chunks end up behind them, so a
            // plain FIFO would drain the backlog from the back after they stop.
            // Frustum-occluded ranking: chunks actually in view are generated
            // first (so sprinting never spends cycles behind the camera), then
            // a small halo behind the player (so 180-degree turns don't show
            // holes), and only lastly the far-behind backlog. Anything not yet
            // in view is still queued, so the world fills in during idle.
            constexpr float COS_VIEW_HALF = 0.66f;   // ~48° half-angle (~46° true half-fov + margin)
            constexpr int BEHIND_HALO = 2;           // ring radius kept high-priority behind the player

            // Shared ranking: lower = generated/meshed sooner. In-view chunks
            // (nearest first, biased to view center and travel direction) beat
            // the near-behind halo, which beats the far-behind backlog.
            auto rankChunk = [centerX, centerZ, dirX, dirZ, viewX, viewZ](int px, int pz) -> float {
                int ox = px - centerX;
                int oz = pz - centerZ;
                int ring = std::max(std::abs(ox), std::abs(oz));
                float vdot = 0.0f;
                float mdot = 0.0f;
                if (ox != 0 || oz != 0) {
                    float inv = 1.0f / std::sqrt(static_cast<float>(ox * ox + oz * oz));
                    vdot = (viewX * ox + viewZ * oz) * inv;
                    mdot = (dirX * ox + dirZ * oz) * inv;
                }
                if (vdot > COS_VIEW_HALF) {
                    return static_cast<float>(ring) - vdot - 0.4f * mdot;
                }
                if (ring <= BEHIND_HALO) {
                    return 1000.0f + static_cast<float>(ring);
                }
                return 2000.0f + static_cast<float>(ring);
            };

            if (pendingGen_.size() > 1) {
                std::sort(pendingGen_.begin(), pendingGen_.end(),
                    [rankChunk](const std::pair<int, int>& a, const std::pair<int, int>& b) {
                        return rankChunk(a.first, a.second) < rankChunk(b.first, b.second);
                    });
            }

            // The mesher drains toMesh_ FIFO; keep its order view-first too so the
            // cone in front of the camera gets drawn before the backlog behind.
            if (toMesh_.size() > 1) {
                std::sort(toMesh_.begin(), toMesh_.end(),
                    [rankChunk](uint64_t a, uint64_t b) {
                        auto unpack = [](uint64_t k) {
                            return std::pair<int, int>{static_cast<int>(k >> 32), static_cast<int>(k & 0xFFFFFFFF)};
                        };
                        auto pa = unpack(a);
                        auto pb = unpack(b);
                        return rankChunk(pa.first, pa.second) < rankChunk(pb.first, pb.second);
                    });
            }
        }
        if (queued > 0) noiseCV_.notify_one();

        flushPendingCleanup();
        processCompletedChunks();
        processGateRemeshResults();
        processEditResults();

        if (playerController_.isCursorCaptured()) {
            playerController_.setVelocityY(Gravity::apply(playerController_.getVelocityY(), static_cast<float>(dt)));
            playerController_.tick(*this, dt);
        }
    }

    std::vector<Chunk*> World::getLoadedChunks() const {
        if (chunkCacheDirty_) {
            chunkCache_.clear();
            chunkCache_.reserve(chunks_.size());
            for (auto& [key, chunk] : chunks_) {
                chunkCache_.push_back(chunk.get());
            }
            chunkCacheDirty_ = false;
        }
        return chunkCache_;
    }

} // namespace lve
