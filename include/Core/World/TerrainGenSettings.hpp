#pragma once

#include "Core/Blocks/Block.hpp"
#include "Core/RegistryKey.hpp"
#include <algorithm>
#include <vector>

namespace kc {

    // Plain-number terrain dials.
    //
    // Everything the heightfield's shape does is decided by four numbers, so this
    // struct is the entire customisation surface: change a value, restart, and
    // the terrain changes. No spline or calculus knowledge is needed to tune it.
    //
    // The defaults are the verbatim Minecraft 26.3 constants transcribed in
    // HomelandsNoise.hpp, so an untouched world generates exactly vanilla terrain.
    // The units below are the ones that make the numbers predictable.
    struct TerrainShape {
        // --- how much the lowlands roll ---------------------------------
        //
        // The inland branch of the offset spline. These three are the lowland
        // valley / plain / tall-hill heights at the extremes of the ridge noise,
        // and their SUM is the entire height range of flat inland terrain.
        // Vanilla's sum is 0.01 + 0.03 + 0.10 = 0.14, which is why inland
        // ground looks like a table.
        //
        // lowlandPlain -- valley floor, the lowest inland ground
        // lowlandHill  -- the gentle middle
        // lowlandTall  -- the highest inland ground
        //
        //   0.01 / 0.03 / 0.10  vanilla, and the defaults below
        //   0.01 / 0.20 / 0.90  rougher experimental variant, not the default
        //
        // Raising lowlandPlain floods the inland above sea level and FLATTENS it
        // (it is both the spline's "low" and "mid" knot, so it pins the whole
        // curve). Hold it near vanilla and raise lowlandTall instead: that adds
        // height range without lifting the valley floor.
        float lowlandPlain = 0.01f;
        float lowlandHill = 0.03f;
        float lowlandTall = 0.10f;

        // --- how bumpy the ground is, in blocks --------------------------
        //
        // A plain number, not a spline knot: the peak-to-trough height of the
        // rolling hills this adds on top of everything else. 0 disables it.
        //
        // Vanilla has no equivalent term, which is the direct reason inland
        // terrain is smooth. Its jaggedness spline only produces a nonzero value
        // when weirdness is within +-0.01 AND ridges is in [0.2, 1.0], so it
        // cannot be dialled -- it is a knife-edge band, not a knob.
        //
        //  0.0   vanilla
        //   4.0   gentle rolling
        //  10.0  proper hills
        // 20.0   mountainous
        float reliefBlocks = 0.0f;

        // --- rivers ------------------------------------------------------
        //
        // A river is the zero-contour network of one extra 2D noise: the set of
        // columns where that noise crosses zero forms branching lines across the
        // map, and every column within `channelHalfWidth + bankWidth` blocks of
        // one is carved down towards a bed just below sea level. The global
        // sea-level fill then holds the water, so a river surface ends up at the
        // same Y as the ocean and the two join seamlessly.
        //
        // The three width/depth dials are TYPICAL values, not fixed ones: two
        // extra noises scale them per column so rivers breathe -- the corridor
        // swings 0.7x..1.3x along its length and the bed depth 0.45x..1.55x --
        // and the bed is profiled from the waterline down to the local depth
        // rather than cut flat. Everything else about a river (where it runs,
        // how wide on average, how deep on average) still comes from these.
        //
        // River LENGTH is not a dial: a fixed ~768-block extent envelope in
        // HomelandsNoise cuts the otherwise-endless contour network into
        // finite arcs (~300-800 blocks each), each tapering to a shallow creek
        // and dry ground at both ends, with a few hundred blocks of gap before
        // the next river starts. It scales corridor and bed depth together and
        // lives entirely in code -- no field here, no metadata tag.
        //
        // All five dials are read from the SAME TerrainShape that HomelandsNoise
        // bakes at build time, so the label (ClimateBiomeProvider), the carve
        // (DefaultTerrainGenerator) and the mask normalisation (the noise object)
        // can never disagree about where a river is.
        //
        // riversEnabled      off = the world generates exactly as before
        // riverWavelength    period of the largest octave, in world blocks. This
        //                    is the spacing dial: bigger = fewer, longer rivers.
        // riverChannelHalf   TYPICAL half-width of the wetted channel, in blocks
        //                    (9 wide at the default 4.5, 6.3..11.7 as it breathes)
        // riverBankWidth     TYPICAL blend from the channel edge back up to
        //                    whatever height the terrain had, in blocks
        // riverDepthBlocks   TYPICAL depth of the bed below sea level, so 5 =
        //                    five blocks of water at the surface at Y63, level
        //                    with the sea, on average: the local bed runs
        //                    roughly 2..8 blocks deep at this default
        bool riversEnabled = true;
        float riverWavelengthBlocks = 384.0f;
        float riverChannelHalfWidth = 4.5f;
        float riverBankWidth = 8.0f;
        float riverDepthBlocks = 5.0f;

        // The offset spline carries an ascending-order assertion, so a dialed
        // value that inverts the knots is clamped instead of throwing out of the
        // noise build. The three lowland dials must stay ascending for the ridge
        // spline they feed.
        void clampToValidRanges() {
            lowlandPlain = std::clamp(lowlandPlain, 0.0f, 2.0f);
            lowlandHill = std::clamp(lowlandHill, 0.0f, 2.0f);
            lowlandTall = std::clamp(lowlandTall, 0.0f, 2.0f);
            lowlandHill = std::max(lowlandHill, lowlandPlain);
            lowlandTall = std::max(lowlandTall, lowlandHill);
            reliefBlocks = std::clamp(reliefBlocks, 0.0f, 96.0f);
            // Below one block of depth the channel floor would sit at or above
            // sea level and hold no water at all, so 1 is the real floor.
            riverWavelengthBlocks = std::clamp(riverWavelengthBlocks, 64.0f, 8192.0f);
            riverChannelHalfWidth = std::clamp(riverChannelHalfWidth, 0.5f, 64.0f);
            riverBankWidth = std::clamp(riverBankWidth, 0.0f, 256.0f);
            riverDepthBlocks = std::clamp(riverDepthBlocks, 1.0f, 32.0f);
        }
    };

    struct TerrainGenSettings {
        struct Layer {
            RegistryKey<Block> block;
            int depth; // layers thick; 0 = fill to bottom
        };

        int seed = 1337;

        // Vanilla sea level. The world spans Y -64..319, matching
        // overworld.json's min_y/height, and vanilla's sea_level is 63 -- so the
        // water table sits 127 blocks above the bottom of the world, not at the
        // old hand-picked 32.
        int seaLevel = 63;

        // Density strictly above zero is solid. Vanilla uses the same test
        // (DensityFunction#isSolid: value > 0).
        float solidThreshold = 0.0f;

        // Terrain shape dials. See TerrainShape: defaults are vanilla, and the
        // four floats are the whole tuning surface.
        TerrainShape shape;
    };

} // namespace kc