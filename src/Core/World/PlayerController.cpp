#include "Core/World/PlayerController.hpp"
#include "Core/KeyBindHandler.hpp"
#include "Core/Keys.hpp"
#include "Core/World/Physics/CollisionSystem.hpp"
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

        // How long the player must be unable to make any progress before the
        // watchdog tries to lift them out. Long enough that a normal pause never
        // trips it, short enough that being wedged is not a long wait.
        constexpr float stuckSeconds = 1.5f;

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
        spawnFinder_.reset();

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
            const SpawnFinder::Result found = spawnFinder_.poll(
                world,
                static_cast<int>(std::floor(spawnPos_.x)),
                static_cast<int>(std::floor(spawnPos_.z)));

            if (found.status == SpawnFinder::Status::Placed) {
                // The finder only picks where on the ground plane the spawn
                // belongs; spawnPos_ keeps its Y and placeOnSurface resolves the
                // body height from the column it reported.
                spawnPos_ = glm::vec3(centeredMin(found.column.x, Attributes::PLAYER_WIDTH),
                                      spawnPos_.y,
                                      centeredMin(found.column.z, Attributes::PLAYER_WIDTH));
                placeOnSurface(world, found.column.x, found.column.surfaceY,
                               found.column.z);
            }

            // A candidate column is not the same as a floor. Hold here until the
            // blocks under the player's feet are actually solid in the world,
            // otherwise the first tick of movement starts with nothing to stand
            // on and the player falls through the terrain.
            if (!spawnPending_ && !hasSolidFloor(world)) {
                spawnPending_ = true;
                spawnFinder_.reset();
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
            if (stuckSeconds_ >= stuckSeconds && !stuckLogged_) {
                stuckLogged_ = true;
                LogUtils::warn(ThreadName::GameLogic,
                    "player: unable to move for "
                    + std::to_string(static_cast<int>(stuckSeconds))
                    + "s (freed=" + (freed ? "yes" : "no")
                    + ", embedded=" + (embedded ? "yes" : "no") + ")");
            }
            if (stuckSeconds_ >= stuckSeconds) recoverFromStuck(world);
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
        spawnFinder_.reset();
    }

    void PlayerController::restore(const glm::vec3& position, float yaw, float pitch, bool snapToSurface) {
        bodyPos_ = position;
        camera_.setPosition(bodyPos_ + glm::vec3(Attributes::PLAYER_WIDTH * 0.5f, Attributes::EYE_HEIGHT, Attributes::PLAYER_WIDTH * 0.5f));
        camera_.setRotation(yaw, pitch);
        // Only a fresh default spawn needs the surface snap; a persisted
        // position keeps its exact Y (e.g. inside a dug-out room / box).
        spawnPending_ = snapToSurface;
        if (snapToSurface) spawnFinder_.reset();
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
        // A respawn is a fresh spawn request: never resume a half-finished sweep.
        spawnFinder_.reset();
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
