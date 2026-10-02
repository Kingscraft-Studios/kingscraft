#include "Core/World/PlayerController.hpp"
#include "Core/KeyBindHandler.hpp"
#include "Core/Keys.hpp"
#include "Core/World/Physics/CollisionSystem.hpp"
#include "Core/Blocks/Blocks.hpp"
#include "Core/World/World.hpp"
#include "Event/EventManager.hpp"
#include "Event/Events/PlayerDeathEvent.hpp"
#include "Event/Events/PlayerRespawnEvent.hpp"
#include "Threads/InputThread.hpp"
#include "Util/LogUtils.hpp"
#include "Vulkan/GLFWWindow.hpp"
#include <algorithm>
#include <cmath>
#include <vector>
#include "Util/TimeUtil.hpp"
#include "Core/Runtime.hpp"

namespace kc {

    namespace {
        constexpr float VOID_KILL_Y = -32.0f;

        // How far the spawn search will walk from the saved spawn point looking
        // for land, counted in whole chunks rather than blocks. A chunk is the
        // natural unit here: each one is asked for its best column in a single
        // heightmap pass, so a wider ring costs a few more reads rather than a
        // few hundred more.
        constexpr int kSpawnSearchChunks = 14;

        // How many chunks one tick is allowed to inspect. The sweep is resumable,
        // so this only sets how smoothly the search spreads across frames; it
        // caps the per-tick cost either way.
        constexpr int kSpawnChunksPerTick = 64;

        // How much of the search area must have actually streamed in before the
        // sweep is allowed to conclude "this is all ocean" and settle for the
        // shallowest column. Without this the search can finish sweeping unloaded
        // chunks and drop the player on the seabed mid-load.
        constexpr int kSpawnMinInspectedChunks = 32;

        // How long the player must be unable to make any progress before the
        // watchdog tries to lift them out. Long enough that a normal pause never
        // trips it, short enough that being wedged is not a long wait.
        constexpr float kStuckSeconds = 1.5f;

        // Converts a block coordinate into the MIN corner of a body of the given
        // width centred on that block.
        //
        // bodyPos_ is an AABB min corner, not a centre: tick() rebuilds the box
        // with AABB::fromPosition(bodyPos_, size) and reads bodyPos_ back out as
        // box.min. Putting it at block + 0.5 therefore straddles the block
        // boundary instead of sitting on it -- a 0.6-wide player spans
        // [x + 0.5, x + 1.1] and overlaps two columns at once. Standing like that,
        // any neighbour a single block higher or lower makes the player embedded
        // or half over a drop, and depenetration then shoves them around the
        // moment they move.
        float centeredMin(int blockCoord, float width) {
            return static_cast<float>(blockCoord) + (1.0f - width) * 0.5f;
        }
    }

    // Places the player on dry ground near the spawn point.
    //
    // The old code took surface + 1 at a fixed column. Water is not collidable
    // (CollisionSystem.cpp) and there is no swimming, so whenever that column
    // happened to be ocean the player was dropped onto the seabed, fully
    // submerged, with no way out -- and the depenetration pass fighting the fall
    // every tick read as the camera drifting on its own.
    //
    // The sweep now works a whole chunk at a time: read that chunk's OCEAN_FLOOR
    // heightmap once, keep its single highest column, and only that one column
    // needs a block lookup to confirm it is dry. Rings come in nearest-first, so
    // the first chunk that yields dry ground wins, and the sweep stops there.
    //
    // Work is capped per tick and the cursor is kept on the controller, so a big
    // search area costs the same per frame as a small one.
    bool PlayerController::tryResolveSpawn(World& world) {
        // Read-only view: World's non-const getChunk overload is private, and
        // the search has no business mutating anything anyway.
        const World& view = world;

        const auto water = Registry<Block>::getRegistry().getShared(Blocks::WATER.getEncoded());
        if (!water) return false;                       // registry not up yet
        const uint64_t waterId = static_cast<uint64_t>(water->getEncodedId());

        const int cs = world.getChunkSize();
        if (cs <= 0) return false;

        // Fast gate. Until the spawn chunk itself has streamed in there is
        // nothing to search outward from.
        const int originGx = static_cast<int>(std::floor(spawnPos_.x / static_cast<float>(cs)));
        const int originGz = static_cast<int>(std::floor(spawnPos_.z / static_cast<float>(cs)));
        if (!view.getChunk(originGx, originGz)) return false;

        int budget = kSpawnChunksPerTick;
        while (budget > 0) {
            // Where we are on the square spiral. Ring 0 is the spawn chunk
            // itself; each later ring is walked one chunk at a time around its
            // perimeter.
            const int ring = spawnSearchRing_;
            if (ring > kSpawnSearchChunks) break;       // sweep finished

            int ringDx = 0, ringDz = 0;
            if (ring == 0) {
                // The spawn chunk. Advance the cursor so it is not re-scanned.
                spawnSearchRing_ = 1;
            } else {
                const int side = 2 * ring;
                const int perim = 4 * side;
                const int i = spawnSearchStep_;
                if (i >= perim) { spawnSearchRing_ = ring + 1; spawnSearchStep_ = 0; continue; }
                if      (i < side)       { ringDx = -ring + i;        ringDz = -ring; }
                else if (i < 2 * side)   { ringDx =  ring;             ringDz = -ring + (i - side); }
                else if (i < 3 * side)   { ringDx =  ring - (i - 2 * side); ringDz =  ring; }
                else                     { ringDx = -ring;             ringDz =  ring - (i - 3 * side); }
                ++spawnSearchStep_;
            }
            --budget;

            const Chunk* chunk = view.getChunk(originGx + ringDx, originGz + ringDz);
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
            ++spawnChunksInspected_;

            const int surfaceY = chunk->localToWorldY(bestLocalY);
            if (surfaceY >= world.getMaxY() - 1) continue;

            // Remember the highest ground anywhere seen. If the whole swept area
            // turns out to be water this is the shallowest point in it, which is
            // a far kinder outcome than the bottom of the sea.
            if (!spawnHaveBest_ || surfaceY > spawnBestSurface_) {
                spawnHaveBest_ = true;
                spawnBestSurface_ = surfaceY;
                spawnBestX_ = (originGx + ringDx) * cs + bestLocalX;
                spawnBestZ_ = (originGz + ringDz) * cs + bestLocalZ;
            }

            // Feet and head must both be clear of liquid, checked on the actual
            // block so this stays correct if water placement changes.
            const int feetLocal = chunk->worldToLocalY(surfaceY + 1);
            const int headLocal = chunk->worldToLocalY(surfaceY + 2);
            const bool submerged =
                chunk->getBlock(bestLocalX, feetLocal, bestLocalZ) == waterId ||
                chunk->getBlock(bestLocalX, headLocal, bestLocalZ) == waterId;
            if (submerged) continue;

            // Nearest dry ground wins, so the player lands as close to the
            // requested point as the map allows.
            const int wx = (originGx + ringDx) * cs + bestLocalX;
            const int wz = (originGz + ringDz) * cs + bestLocalZ;
            spawnPos_ = glm::vec3(centeredMin(wx, Attributes::PLAYER_WIDTH), spawnPos_.y,
                                  centeredMin(wz, Attributes::PLAYER_WIDTH));
            resetSpawnSearch();
            LogUtils::info(ThreadName::GameLogic,
                "spawn: placed on dry land at (" + std::to_string(wx) + ", " +
                std::to_string(surfaceY) + ", " + std::to_string(wz) + ")");
            placeOnSurface(world, wx, surfaceY, wz);
            return true;
        }

        // Swept the whole area without finding dry ground. Commit to the
        // shallowest column rather than leaving the player falling forever --
        // but only once enough of the area has actually loaded to justify
        // concluding it really is all ocean. Otherwise restart the sweep and let
        // more chunks stream in first.
        if (spawnSearchRing_ > kSpawnSearchChunks) {
            if (spawnChunksInspected_ < kSpawnMinInspectedChunks) {
                resetSpawnSearch();
                return false;
            }
            if (spawnHaveBest_) {
                const int surfaceY = spawnBestSurface_;
                const int wx = spawnBestX_;
                const int wz = spawnBestZ_;
                const int inspected = spawnChunksInspected_;
                spawnPos_ = glm::vec3(centeredMin(wx, Attributes::PLAYER_WIDTH), spawnPos_.y,
                                      centeredMin(wz, Attributes::PLAYER_WIDTH));
                resetSpawnSearch();
                LogUtils::warn(ThreadName::GameLogic,
                    "spawn: no dry land within " + std::to_string(kSpawnSearchChunks) +
                    " chunks (" + std::to_string(inspected) +
                    " loaded chunks inspected); settling on the shallowest water at (" +
                    std::to_string(wx) + ", " + std::to_string(surfaceY) + ", " +
                    std::to_string(wz) + ")");
                placeOnSurface(world, wx, surfaceY, wz);
                return true;
            }
        }

        return false;
    }

    void PlayerController::resetSpawnSearch() {
        spawnSearchRing_ = 0;
        spawnSearchStep_ = 0;
        spawnChunksInspected_ = 0;
        spawnHaveBest_ = false;
        spawnBestSurface_ = 0;
        spawnBestX_ = 0;
        spawnBestZ_ = 0;
    }

    void PlayerController::placeOnSurface(World& world, int x, int surface, int z) {
        const float y = static_cast<float>(
            std::clamp(surface, world.getMinY(), world.getMaxY())) + 1.0f;
        bodyPos_ = glm::vec3(centeredMin(x, Attributes::PLAYER_WIDTH), y,
                             centeredMin(z, Attributes::PLAYER_WIDTH));
        // Any fall velocity carried over from the wait would be applied on the
        // same tick and shove the player straight back down.
        velocityY_ = 0.0f;
        jumpedThisAirTime_ = false;
        spawnPending_ = false;
        stuckSeconds_ = 0.0f;
        camera_.setPosition(bodyPos_ + glm::vec3(Attributes::PLAYER_WIDTH * 0.5f,
                                                 Attributes::EYE_HEIGHT,
                                                 Attributes::PLAYER_WIDTH * 0.5f));
    }

    // True when the player has a real, loaded, solid floor to stand on.
    //
    // The spawn sweep already picked a column whose chunk was loaded, but that
    // says nothing about the blocks actually sitting under the player's feet:
    // World::getBlock answers air (0) for any chunk that has not streamed in, so
    // a spawn chosen from a heightmap can still land on nothing. Gravity is
    // applied before tick() runs, so releasing the spawn over empty space drops
    // the player straight through the world and into a sealed pocket inside the
    // terrain, where there is no collision to push them out.
    //
    // Every corner of the footprint is checked, not just the centre, because the
    // player is 0.6 blocks wide and a floor that exists only under half of them
    // tips them into the gap as soon as they move.
    bool PlayerController::hasSolidFloor(const World& world) const {
        const AABB box = AABB::fromPosition(
            bodyPos_, glm::vec3(Attributes::PLAYER_WIDTH, Attributes::PLAYER_HEIGHT,
                                Attributes::PLAYER_WIDTH));

        int x0 = static_cast<int>(std::floor(box.min.x));
        int x1 = static_cast<int>(std::floor(box.max.x - 1.0e-4f));
        int z0 = static_cast<int>(std::floor(box.min.z));
        int z1 = static_cast<int>(std::floor(box.max.z - 1.0e-4f));
        const int feetY = static_cast<int>(std::floor(box.min.y)) - 1;

        for (int z = z0; z <= z1; ++z) {
            for (int x = x0; x <= x1; ++x) {
                if (!CollisionSystem::isSolidBlock(world, x, feetY, z)) return false;
            }
        }
        return true;
    }

    // Last-resort rescue for a player the collision pass could not free.
    //
    // CollisionSystem::moveEntity can report failure (wedged with no free cell
    // above and every horizontal escape blocked), and a player can also come to
    // rest inside a sealed air pocket under the terrain where there is simply no
    // collision to resolve against. Both used to end in silence: the player was
    // frozen in place with no way out and nothing in the log.
    //
    // The recovery is deliberately conservative -- walk straight up and take the
    // first spot that fits the body AND has solid ground under it. It never picks
    // a position the player could not have reached, so it cannot drop them out of
    // the world; if nothing qualifies the player is left alone to try again next
    // tick, which happens naturally while the chunks are still streaming in.
    bool PlayerController::recoverFromStuck(World& world) {
        const glm::vec3 size(Attributes::PLAYER_WIDTH, Attributes::PLAYER_HEIGHT,
                             Attributes::PLAYER_WIDTH);
        const glm::vec3 start = bodyPos_;

        int from = static_cast<int>(std::floor(start.y)) + 1;
        for (int y = from; y < world.getMaxY() - 1; ++y) {
            glm::vec3 cand = start;
            cand.y = static_cast<float>(y);
            bodyPos_ = cand;
            if (CollisionSystem::aabbCollides(world, AABB::fromPosition(cand, size))) continue;
            if (!hasSolidFloor(world)) continue;

            velocityY_ = 0.0f;
            jumpedThisAirTime_ = false;
            stuckSeconds_ = 0.0f;
            camera_.setPosition(bodyPos_ + glm::vec3(Attributes::PLAYER_WIDTH * 0.5f,
                                                     Attributes::EYE_HEIGHT,
                                                     Attributes::PLAYER_WIDTH * 0.5f));
            LogUtils::warn(ThreadName::GameLogic,
                "player: recovered from being stuck, lifted to y=" + std::to_string(y));
            return true;
        }

        bodyPos_ = start;
        return false;
    }

    void PlayerController::init(KeyBindHandler& keybinds) {
        keybinds_ = &keybinds;
        lastMouseX_ = Runtime::get().inputThread->getLastX();
        lastMouseY_ = Runtime::get().inputThread->getLastY();
        bodyPos_ = spawnPos_;
        velocityY_ = 0.0f;
        resetSpawnSearch();

        camera_.setPosition(bodyPos_ + glm::vec3(Attributes::PLAYER_WIDTH * 0.5f, Attributes::EYE_HEIGHT, Attributes::PLAYER_WIDTH * 0.5f));
        camera_.setRotation(0.0f, -35.0f);

        keybinds_->onPress(BindLayer::Screen, {Keys::SPACE}, [this]() {
            if (cursorCaptured_) jumpPresses_.push_back(TimeUtil::uptimeSeconds());
        });
    }

    void PlayerController::tick(World& world, double dt) {
        if (!keybinds_ || !Runtime::get().inputThread->isInitialized()) return;
        if (dead_) return;
        if (!cursorCaptured_) return;

        if (spawnPending_) {
            // Deliberately does nothing until it can place the player on dry
            // land: committing to a half-streamed world is what used to drop the
            // player onto an ocean floor.
            tryResolveSpawn(world);

            // A candidate column is not the same as a floor. Hold here until the
            // blocks under the player's feet are actually solid in the world,
            // otherwise the first tick of movement starts with nothing to stand
            // on and the player falls through the terrain.
            if (!spawnPending_ && !hasSolidFloor(world)) {
                spawnPending_ = true;
                resetSpawnSearch();
            }

            // Still searching, so hold the player in place. Letting gravity run
            // here is what sank them to the seabed in the first place: the sweep
            // needs a moment to walk outward, and the player was already falling
            // through non-collidable water while it did.
            if (spawnPending_) {
                velocityY_ = 0.0f;
                camera_.setPosition(bodyPos_ + glm::vec3(Attributes::PLAYER_WIDTH * 0.5f,
                                                         Attributes::EYE_HEIGHT,
                                                         Attributes::PLAYER_WIDTH * 0.5f));
                return;
            }
        }

        const float dtf = static_cast<float>(dt);
        const glm::vec3 forward = camera_.getForward();
        const glm::vec3 forwardH = glm::normalize(glm::vec3(forward.x, 0.0f, forward.z));
        const glm::vec3 rightH = glm::normalize(glm::cross(forwardH, glm::vec3(0.0f, 1.0f, 0.0f)));

        glm::vec3 velocity(0.0f);
        AABB box = AABB::fromPosition(bodyPos_,
            glm::vec3(Attributes::PLAYER_WIDTH, Attributes::PLAYER_HEIGHT, Attributes::PLAYER_WIDTH));
        if (respawnGrace_ > 0.0f) {
            respawnGrace_ -= dtf;
            jumpPresses_.clear();
        } else if (flyMode_) {
            // Consume the press log: a double-tap mid-air restores gravity.
            for (double ts : jumpPresses_) {
                if (ts - lastFlyTap_ < FLY_TOGGLE_WINDOW) {
                    flyMode_ = false;
                    jumpedThisAirTime_ = false;
                    lastFlyTap_ = -1.0e9;
                } else {
                    lastFlyTap_ = ts;
                }
            }
            jumpPresses_.clear();

            // Gravity is suppressed by World.cpp while flying; pilot vertical.
            if (keybinds_->isDown(Keys::W)) velocity += forwardH * Attributes::FLY_SPEED;
            if (keybinds_->isDown(Keys::S)) velocity -= forwardH * Attributes::FLY_SPEED;
            if (keybinds_->isDown(Keys::A)) velocity -= rightH * Attributes::FLY_SPEED;
            if (keybinds_->isDown(Keys::D)) velocity += rightH * Attributes::FLY_SPEED;
            if (keybinds_->isDown(Keys::SPACE)) {
                velocityY_ = Attributes::FLY_CLIMB_SPEED;
            } else if (keybinds_->isDown(Keys::LEFT_SHIFT) || keybinds_->isDown(Keys::RIGHT_SHIFT)) {
                velocityY_ = -Attributes::FLY_DESCEND_SPEED;
            } else {
                velocityY_ = 0.0f;
            }
        } else {
            if (keybinds_->isDown(Keys::W)) velocity += forwardH * Attributes::WALK_SPEED;
            if (keybinds_->isDown(Keys::S)) velocity -= forwardH * Attributes::WALK_SPEED;
            if (keybinds_->isDown(Keys::A)) velocity -= rightH * Attributes::WALK_SPEED;
            if (keybinds_->isDown(Keys::D)) velocity += rightH * Attributes::WALK_SPEED;

            bool grounded = CollisionSystem::aabbCollides(world, box.translate(glm::vec3(0.0f, -0.05f, 0.0f)));
            if (grounded) {
                velocityY_ = 0.0f;
                jumpedThisAirTime_ = false;
                if (!jumpPresses_.empty()) {
                    velocityY_ = Attributes::JUMP_STRENGTH;
                    jumpedThisAirTime_ = true;
                }
            } else if (!jumpPresses_.empty() && jumpedThisAirTime_) {
                // DOUBLE JUMP: in the air after a real jump -> gravity OFF.
                flyMode_ = true;
                jumpedThisAirTime_ = false;
                velocityY_ = Attributes::FLY_CLIMB_SPEED;
            }
            jumpPresses_.clear();
        }
        velocity.y = velocityY_;

        const bool freed = CollisionSystem::moveEntity(world, box, velocity, dtf);
        velocityY_ = velocity.y;
        bodyPos_ = box.min;

        // Stuck watchdog.
        //
        // Two separate failure modes have to be caught, and neither shows up as
        // "the player moved somewhere wrong":
        //
        //  1. moveEntity could not free the box. That is a hard signal -- the body
        //     is still inside solid ground.
        //  2. The body is resting but its own box overlaps solid, which means the
        //     depenetration pass gave up (its iteration budget ran out).
        //
        // A third case is a sealed air pocket under the terrain: no collision at
        // all, gravity zeroed by the floor each tick, and the player cannot get
        // out. That one is caught by watching for a stationary body that is not
        // grounded and not falling -- standing still on flat ground is grounded,
        // and a real fall keeps velocityY_ negative, so neither trips the timer.
        //
        // Flying is exempt throughout: the player is meant to hold a fixed
        // position there.
        const glm::vec3 bodySize(Attributes::PLAYER_WIDTH, Attributes::PLAYER_HEIGHT,
                                 Attributes::PLAYER_WIDTH);
        const bool embedded =
            CollisionSystem::aabbCollides(world, AABB::fromPosition(bodyPos_, bodySize));
        const bool grounded =
            CollisionSystem::aabbCollides(world, AABB::fromPosition(bodyPos_, bodySize)
                                                             .translate(glm::vec3(0.0f, -0.05f, 0.0f)));
        const bool stationary = bodyPos_ == lastBodyPos_;
        const bool hanging = stationary && velocityY_ == 0.0f && !grounded;

        if (!flyMode_ && ( !freed || embedded || hanging)) {
            stuckSeconds_ += dtf;
            if (stuckSeconds_ >= kStuckSeconds && !stuckLogged_) {
                stuckLogged_ = true;
                LogUtils::warn(ThreadName::GameLogic,
                    "player: unable to move for "
                    + std::to_string(static_cast<int>(kStuckSeconds))
                    + "s (freed=" + (freed ? "yes" : "no")
                    + ", embedded=" + (embedded ? "yes" : "no") + ")");
            }
            if (stuckSeconds_ >= kStuckSeconds) recoverFromStuck(world);
        } else {
            stuckSeconds_ = 0.0f;
            stuckLogged_ = false;
        }
        lastBodyPos_ = bodyPos_;

        if (bodyPos_.y < VOID_KILL_Y) {
            dead_ = true;
            velocityY_ = 0.0f;
            flyMode_ = false;
            jumpedThisAirTime_ = false;
            jumpPresses_.clear();
            EventManager::get().callEvent<PlayerDeathEvent>(DeathCause::Void);
            return;
        }

        camera_.setPosition(bodyPos_ + glm::vec3(Attributes::PLAYER_WIDTH * 0.5f, Attributes::EYE_HEIGHT, Attributes::PLAYER_WIDTH * 0.5f));

        double mx = Runtime::get().inputThread->getLastX();
        double my = Runtime::get().inputThread->getLastY();
        double dx = mx - lastMouseX_;
        double dy = lastMouseY_ - my;
        lastMouseX_ = mx;
        lastMouseY_ = my;

        if (dx != 0.0 || dy != 0.0) {
            camera_.rotate(static_cast<float>(dx) * 0.1f, static_cast<float>(dy) * 0.1f);
        }
    }

    void PlayerController::setSpawn(const glm::vec3& spawn) {
        spawnPos_ = spawn;
        // A new spawn point invalidates whatever the previous sweep had found.
        resetSpawnSearch();
    }

    void PlayerController::restore(const glm::vec3& position, float yaw, float pitch, bool snapToSurface) {
        bodyPos_ = position;
        camera_.setPosition(bodyPos_ + glm::vec3(Attributes::PLAYER_WIDTH * 0.5f, Attributes::EYE_HEIGHT, Attributes::PLAYER_WIDTH * 0.5f));
        camera_.setRotation(yaw, pitch);
        // Only a fresh default spawn needs the surface snap; a persisted
        // position keeps its exact Y (e.g. inside a dug-out room / box).
        spawnPending_ = snapToSurface;
        if (snapToSurface) resetSpawnSearch();
        flyMode_ = false;
        jumpedThisAirTime_ = false;
        jumpPresses_.clear();
        lastFlyTap_ = -1.0e9;
    }

    void PlayerController::respawn() {
        bodyPos_ = spawnPos_;
        velocityY_ = 0.0f;
        respawnGrace_ = 0.4f;
        spawnPending_ = true;
        dead_ = false;
        flyMode_ = false;
        jumpedThisAirTime_ = false;
        jumpPresses_.clear();
        lastFlyTap_ = -1.0e9;
        EventManager::get().callEvent<PlayerRespawnEvent>();
    }

    void PlayerController::updateProjection(VkExtent2D extent, float fov, float nearPlane, float farPlane) {
        float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        camera_.setAspectRatio(aspect);
        camera_.setFov(fov);
        camera_.setNearPlane(nearPlane);
        camera_.setFarPlane(farPlane);
    }

    void PlayerController::resetMouse() {
        if (Runtime::get().inputThread->isInitialized()) {
            lastMouseX_ = Runtime::get().inputThread->getLastX();
            lastMouseY_ = Runtime::get().inputThread->getLastY();
        }
    }

    glm::mat4 PlayerController::getViewProj() const {
        return camera_.getProjectionMatrix() * camera_.getViewMatrix();
    }

} // namespace kc
