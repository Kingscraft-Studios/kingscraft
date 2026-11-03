#pragma once

namespace kc {

    class World;

    // Resolves where the player's feet go when a spawn has to be snapped to the
    // terrain. It owns the resumable chunk sweep so a search can span many ticks
    // without any per-tick state living on the player, and it never moves the
    // player or mutates the world: it inspects loaded chunks and reports a
    // column for the caller to apply.
    //
    // Water is not collidable and there is no swimming, so landing on the seabed
    // is not a spawn: the sweep must find dry ground. It works one chunk at a
    // time (single OCEAN_FLOOR heightmap pass, highest column kept) in
    // nearest-first rings, and only settles for the shallowest water when the
    // whole search area has streamed in and is genuinely all ocean.
    class SpawnFinder {
    public:
        struct Column {
            int x = 0;          // world block coordinates
            int surfaceY = 0;   // top solid block; feet go one block above it
            int z = 0;
            // False when the sweep found no dry land and settled for the
            // shallowest water column instead.
            bool dry = false;
        };

        enum class Status {
            Searching,  // no answer yet; poll again next tick
            Placed      // Result::column is ready to apply
        };

        struct Result {
            Status status = Status::Searching;
            Column column;
        };

        // Drops any partial sweep. Call whenever a new spawn request starts:
        // world init, a new spawn point, a surface-restoring load, or respawn.
        void reset();

        // Advances the sweep by at most the per-tick chunk budget.
        // originX/originZ are the world block coordinates the player asked to
        // spawn near, not the last column this finder reported.
        Result poll(const World& world, int originX, int originZ);

    private:
        // Cursor for the chunk-by-chunk sweep, so no single tick ever has to
        // walk the whole search area. The sweep resumes where it left off.
        int ring_ = 0;
        int step_ = 0;
        // Loaded, non-empty chunks looked at so far this sweep. The sweep is only
        // allowed to give up on finding land once this is high enough to prove
        // the surrounding terrain has actually streamed in; otherwise it would
        // settle for the seabed just because it looked while the world was still
        // loading.
        int inspected_ = 0;
        // Shallowest column seen so far, used when the whole swept area turns out
        // to be water: better the top of a sandbar than the bottom of the sea.
        int bestSurface_ = 0;
        int bestX_ = 0;
        int bestZ_ = 0;
        bool haveBest_ = false;
    };

} // namespace kc
