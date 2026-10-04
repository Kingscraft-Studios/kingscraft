#include "Core/World/Biomes/MountainsBiome.hpp"

#include "Core/Blocks/Blocks.hpp"
#include "Core/World/Biomes/ClimateBiomeProvider.hpp"

#include <cmath>

namespace kc {
    MountainsBiome::MountainsBiome() {
        layers = {
            {Blocks::GRASS_BLOCK, 1}, // grass cap on the lower slopes
            {Blocks::DIRT,        3}, // soil under that cap
            {Blocks::STONE,       0}, // fill to bottom
        };
    }

    // Above this the mountains go bare. Vanilla switches exposed rock on a
    // gradient test (minecraft:steep) rather than a height, which needs neighbour
    // columns and is not wired up here; a height band is the honest stand-in.
    static constexpr int bareRockY = 95;

    // How far the band may be pushed up or down by the per-column surface noise.
    //
    // At zero wobble the rock boundary was a perfectly level contour: measured
    // over the seed-1337 dump it was exactly Y=95 for every column, with 0 stone
    // tops below Y=96 and 0 grass tops above. A contour that is flat everywhere
    // is the artefact -- it puts bare rock halfway up a cliff face at Y=96 and on
    // a gentle plateau at Y=96 alike, which is where it stops reading as an
    // alpine line and starts reading as a drawn line.
    //
    // terrain/surface_noise has layer periods of 64/32/16 blocks, so its finest
    // detail is 16 blocks: this bends the boundary into a gentle wave rather than
    // speckling it. That is the right trade at this frequency -- a per-column
    // draw from this same field would produce coherent 16-block blobs that
    // correlate with the terrain that generated them. A genuinely ragged edge
    // would need a higher-frequency node, which is not wired up here.
    static constexpr float treelineWobble = 4.0f;

    bool MountainsBiome::useBareRock(int worldTopY, float dither, const Climate& climate) const {
        if (climate.mountain < ClimateBiomeProvider::treelineBiome) {
            return false;
        }
        // This used to read `ridges > 0.25 || mountain > 0.45` and only fall
        // through to the height test when both failed. That disjunction is now
        // always true: naming a column MOUNTAINS already requires its mountain
        // weight to be at least every other weight, which over the measured
        // 95883 mountains columns never fell below 0.376, and never left the
        // ridges term false either. So the branch it guarded was unreachable and
        // the rule is exactly "bare above the treeline". Kept as a single
        // comparison so the rock line can be read off directly instead of
        // inferred.
        const int wobble = static_cast<int>(std::lround(treelineWobble * dither));
        return worldTopY > bareRockY + wobble;
    }
}