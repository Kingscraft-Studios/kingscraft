#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "Core/World/Generation/Noise/BlendedNoise.hpp"
#include "Core/World/Generation/Density/DensityGraph.hpp"
#include "Core/World/Generation/Random/RandomSource.hpp"
#include "Core/World/Generation/Random/XoroshiroRandom.hpp"
#include "Core/World/TerrainGenSettings.hpp"

namespace kc {

    // Builds the no-cave overworld density graph from vanilla's NoiseRouterData.
    //
    // Every constant and knot here is transcribed from
    //   net/minecraft/world/level/levelgen/NoiseRouterData.java  (overworld router)
    //   net/minecraft/data/worldgen/TerrainProvider.java          (spline tables)
    //   data/minecraft/worldgen/noise/*.json                     (noise parameters)
    // for Minecraft 26.3. The graph is identical to vanilla's for a non-amplified,
    // legacy_random_source:false overworld with caves and structures disabled, and
    // differs from the JSON preset only in dropping `initial_density_without_jaggedness`,
    // `ridges_folded`, `cave_entrances`, `badlands_pillar`, `bubble_column`, the ore
    // vein functions, and the final_density beardifier/ore-vein terms.
    //
    // The terrain shape is driven entirely by four climate noises through
    // TerrainProvider's splines:
    //   continentalness -> offset (average surface height) and factor (vertical scale)
    //   erosion         -> both of the above
    //   weirdness       -> factor, and peaks-and-valleys for jaggedness
    //   ridges          -> the spline values themselves
    class HomelandsNoise {
    public:
        static constexpr int minY = -64;
        static constexpr int height = 384;
        static constexpr int maxY = minY + height;  // 320 exclusive top
        static constexpr int seaLevel = 63;

        struct Rngs {
            std::unique_ptr<RandomSource> root;
            std::unique_ptr<PositionalRandomFactory> rootFactory;
        };

        // Mirrors Noises.instantiate: the world seed is turned into a positional
        // factory, and each noise is then keyed by its registry id.
        static Rngs makeRngs(int64_t worldSeed) {
            auto root = std::make_unique<XoroshiroRandom>(worldSeed);
            auto factory = root->forkPositionalFactory();
            return Rngs{std::move(root), std::move(factory)};
        }

        // Noises.instantiate seeds each noise with its ResourceKey identifier, i.e.
        // "<namespace>:" + the Noises enum name. Those names are NOT dimension
        // prefixed: Noises.SHIFT is "offset", Noises.RIDGE is "ridge" and
        // Noises.CONTINENTALNESS is "continentalness" (only the Nether entries
        // carry a "nether/" prefix). The per-octave seeds are then
        // NormalNoise.OctaveInfo.seed() == "octave_<index>".
        static NormalNoise::Parameters parameters(const std::string& id) {
            // Transcribed from data/minecraft/worldgen/noise/<id>.json.
            if (id == "offset") {
                return {0.9381732587751005, -3, 4, NormalNoise::Normalization::Enabled,
                        {1.0, 1.0, 1.0, 0.0}};
            }
            if (id == "temperature") {
                return {1.2453007926713473, -10, 6, NormalNoise::Normalization::Enabled,
                        {1.5, 0.0, 1.0, 0.0, 0.0, 0.0}};
            }
            if (id == "vegetation") {
                return {0.9494731054427978, -8, 6, NormalNoise::Normalization::Enabled,
                        {1.0, 1.0, 0.0, 0.0, 0.0, 0.0}};
            }
            if (id == "continentalness") {
                return {0.8880832896205223, -9, 9, NormalNoise::Normalization::Enabled,
                        {1.0, 1.0, 2.0, 2.0, 2.0, 1.0, 1.0, 1.0, 1.0}};
            }
            if (id == "erosion") {
                return {1.063180125160734, -9, 5, NormalNoise::Normalization::Enabled,
                        {1.0, 1.0, 0.0, 1.0, 1.0}};
            }
            if (id == "ridge") {
                return {0.9147152149950137, -7, 6, NormalNoise::Normalization::Enabled,
                        {1.0, 2.0, 1.0, 0.0, 0.0, 0.0}};
            }
            if (id == "surface") {
                // noise/surface.json: no amplitude modifiers, 3 octaves.
                return {0.9381732587751008, -6, 3, NormalNoise::Normalization::Enabled, {}};
            }
            if (id == "jagged") {
                // jagged.json sets no amplitude modifiers, so all octaves stay 1.0.
                return {1.0383104856073737, -16, 16, NormalNoise::Normalization::Enabled, {}};
            }
            if (id == "kingscraft_relief") {
                // Not a vanilla noise -- the project's own relief field, driven by
                // TerrainShape::reliefBlocks, measured in blocks of rolling hills.
                //
                // Scale: this is sampled through noise2d(..., xzScale = 0.25), so
                // one period spans 2^-firstOctave / 0.25 world blocks. firstOctave
                // -5 therefore puts the largest octave at ~128 blocks and the
                // finest at ~16 -- hills, not continents. (firstOctave -8, the
                // first attempt, gives 1024-block swells that raise the shoreline
                // instead of adding relief, which is measurably the wrong thing.)
                //
                // The falling amplitude weights keep the wide shapes dominant so
                // the result reads as hills rather than as per-block roughness.
                return {1.0f, -5, 4, NormalNoise::Normalization::Enabled,
                        {1.0f, 0.6f, 0.35f, 0.2f}};
            }
            if (id == "kingscraft_river") {
                // Not a vanilla noise: the field whose ZERO CONTOUR is the river
                // centreline network. Branching lines are what you get for free
                // from the level set n(x,z) == 0 of a smooth field, which is why
                // one noise buys a whole river system instead of a blob map.
                //
                // Only two octaves, and the second is damped: extra octaves add
                // high-frequency wiggle to the contour but also make the
                // gradient-normalised distance below noisier, so the channel
                // width would jitter along its own length.
                return {1.0f, -6, 2, NormalNoise::Normalization::Enabled,
                        {1.0f, 0.35f}};
            }
            if (id == "kingscraft_river_depth") {
                // Not a vanilla noise: how deep the river bed sits at a column.
                // Sampled TWICE through noise2d at different scales (see the
                // build below): a slow ~160-block pass scales the depth dial
                // into long deep runs and shallow pools along the river, a fast
                // ~24-block pass roughs the bottom so the bed is not a smooth
                // trough. Falling octave weights keep both passes smooth rather
                // than grainy -- the bed should roll, not jitter.
                return {1.0f, -6, 3, NormalNoise::Normalization::Enabled,
                        {1.0f, 0.5f, 0.25f}};
            }
            if (id == "kingscraft_river_width") {
                // Not a vanilla noise: the corridor width multiplier, sampled at
                // a ~240-block period so a river breathes wide and narrow over
                // stretches much longer than one bend. The period relation is
                // the kingscraft_river one: period = 2^(-firstOctave) / xzScale.
                // Its own noise (not the depth one) so width and depth stay
                // decorrelated -- a narrow gorge need not also be a deep one.
                return {1.0f, -6, 2, NormalNoise::Normalization::Enabled,
                        {1.0f, 0.4f}};
            }
            if (id == "kingscraft_river_extent") {
                // Not a vanilla noise, and not a dial either: the fixed extent
                // envelope that cuts the endless contour network into finite
                // rivers. One noise with a period set in code (see
                // riverExtentWavelength): where the field is low the corridor
                // pinches to nothing and the river dries up, where it is high
                // the river runs full width, so every river is an arc of a few
                // hundred blocks with a gap before the next one -- instead of a
                // closed loop that wanders the whole map. The damping weights
                // keep the envelope as smooth as the river field itself: the
                // ends of a river must fade, never fray.
                return {1.0f, -6, 2, NormalNoise::Normalization::Enabled,
                        {1.0f, 0.35f}};
            }
            return {1.0, 0, 1, NormalNoise::Normalization::Enabled, {}};
        }

        DensityGraph graph;
        int finalDensity = -1;
        int initialDensity = -1;
        int slopedCheese = -1;
        int offset = -1;
        int factor = -1;
        int depth = -1;
        int jaggedNoise = -1;
        int base3d = -1;
        int ridges = -1;
        int temperature = -1;
        int vegetation = -1;
        int continentalness = -1;
        int erosion = -1;
        int preliminarySurfaceLevel = -1;
        int chunkSurfaceLevel = -1;
        int surfaceNoise = -1;
        int surfaceUpperBound = -1;
        int river = -1;
        int riverDepthVar = -1;
        int riverBedBump = -1;
        int riverWidthVar = -1;
        int riverExtentVar = -1;
        int zero = -1;

        HomelandsNoise() = default;

        explicit HomelandsNoise(int64_t worldSeed) { build(worldSeed); }

        // The shape dials only move spline knots, so they are read once at build
        // time and baked into the graph. There is no per-sample dial lookup.
        HomelandsNoise(int64_t worldSeed, const kc::TerrainShape& shape) { build(worldSeed, shape); }

        void build(int64_t worldSeed) { build(worldSeed, kc::TerrainShape{}); }

        void build(int64_t worldSeed, const kc::TerrainShape& shape) {
            Rngs rngs = makeRngs(worldSeed);
            buildFromFactory(*rngs.rootFactory, shape);
        }

        void buildFromFactory(const PositionalRandomFactory& rootFactory) {
            buildFromFactory(rootFactory, kc::TerrainShape{});
        }

        void buildFromFactory(const PositionalRandomFactory& rootFactory, const kc::TerrainShape& shape) {
            zero = graph.zero();

            // River dials are baked here alongside the spline knots so that the
            // noise object is the single source of truth: ClimateBiomeProvider
            // only holds a reference to this, and carveRiverTop() below is the
            // ONE implementation of the carve that both it and the generator
            // call. Reading the dials from a second copy of TerrainShape would
            // let the label drift away from the trench it is supposed to name.
            riverEnabled_ = shape.riversEnabled;
            riverCorridorHalf_ = shape.riverChannelHalfWidth + shape.riverBankWidth;
            // The mask is 1 - distance/corridor, so the channel edge
            // (distance = channelHalf) sits at mask = bank/corridor. Dividing the
            // widths the other way round gives channel/corridor, which is the
            // number that looks right and is not: it carves a channel as wide as
            // the whole corridor.
            riverChannelEdge_ = riverCorridorHalf_ > 0.0f
                                    ? (riverCorridorHalf_ - shape.riverChannelHalfWidth) / riverCorridorHalf_
                                    : 0.0f;
            riverDepth_ = shape.riverDepthBlocks;
            // The width field scales the corridor per column, so the corridor
            // can be at most the dial times the widest swing the width field
            // reaches; that maximum is what the distance test below can early-
            // out against, and what the stencil has to span.
            riverCorridorMax_ = riverCorridorHalf_ * (1.0f + riverWidthSwing);
            // Stencil width for the secant gradient below: as wide as the
            // WIDEST corridor, which is the distance the estimate has to cover,
            // with a floor so a narrow corridor still steps over single-block
            // slope noise rather than dividing by it.
            riverStencil_ = std::clamp(static_cast<int>(std::lround(riverCorridorMax_)), 4, 32);

            // ---- registered noises ---------------------------------------
            const int shiftNoise = addNoise(rootFactory, "offset");
            const int temperatureNoise = addNoise(rootFactory, "temperature");
            const int vegetationNoise = addNoise(rootFactory, "vegetation");
            const int continentsNoise = addNoise(rootFactory, "continentalness");
            const int erosionNoise = addNoise(rootFactory, "erosion");
            const int ridgeNoise = addNoise(rootFactory, "ridge");
            const int jaggedNoise = addNoise(rootFactory, "jagged");
            // Not a vanilla noise: the tunable relief term (TerrainShape).
            const int reliefNoise = addNoise(rootFactory, "kingscraft_relief");
            // Not a vanilla noise: the river centreline field (TerrainShape).
            const int riverNoise = addNoise(rootFactory, "kingscraft_river");
            // Not vanilla either: bed depth, bed roughness, corridor width and
            // the fixed extent envelope.
            const int riverDepthNoise = addNoise(rootFactory, "kingscraft_river_depth");
            const int riverWidthNoise = addNoise(rootFactory, "kingscraft_river_width");
            const int riverExtentNoise = addNoise(rootFactory, "kingscraft_river_extent");

            const int shiftX = graph.shiftA(shiftNoise);
            const int shiftZ = graph.shiftB(shiftNoise);
            graph.registerFunction("shift_x", shiftX);
            graph.registerFunction("shift_z", shiftZ);

            // Relief: a plain-number rolling-hills term with no vanilla equivalent.
            //
            // Vanilla's own ruggedness term (jaggedness) is unusable as a knob --
            // its spline only produces a nonzero value when weirdness is within
            // +-0.01 AND ridges is in [0.2, 1.0], so it is a knife-edge band
            // rather than something you can dial. Measured across a 12k-block
            // sweep, moving its erosion knot changes the terrain by exactly zero.
            //
            // So this adds its own 2D field to `offset`, which is the term that
            // decides average surface height. It is added to `offset` itself and
            // not to `depth`, so preliminarySurfaceLevel sees it too and the
            // top-down scan in TerrainGenerator::columnTop still starts on air.
            //
            // Height conversion: depth = yLinearGradient(-64,320, 1.5,-1.5) +
            // offset, and that gradient falls 3.0 over 384 blocks, i.e. 0.0078125
            // per block. The surface sits where depth == 0, so one block of
            // height is 1/128 of offset. offsetPerBlock converts blocks -> offset.
            //
            // At reliefBlocks == 0 the multiplier is exactly 0.0f and add() with
            // a zero constant is the identity in IEEE-754, so the graph evaluates
            // bit-identically to vanilla.
            constexpr float offsetPerBlock = 1.0f / 128.0f;

            // BlendedNoise seeds from the factory with the literal "minecraft:terrain"
            // id, NOT from the world seed's octave names.
            {
                const std::unique_ptr<RandomSource> terrainRandom =
                    rootFactory.fromHashOf(BlendedNoise::noiseSeed);
                BlendedNoise blended = BlendedNoise::homelands();
                blended.create(*terrainRandom);
                graph.addBlended(std::move(blended));
                // addBlended() returns an index into the graph's BlendedNoise
                // storage, not a node id; blended() is what mints the node.
                base3d = graph.blended(0);
                graph.registerFunction("terrain/base_3d_noise", base3d);
            }

            // ---- 2D climate noises --------------------------------------
            temperature = graph.noise2d(shiftX, shiftZ, 0.25, temperatureNoise);
            vegetation = graph.noise2d(shiftX, shiftZ, 0.25, vegetationNoise);
            continentalness = graph.noise2d(shiftX, shiftZ, 0.25, continentsNoise);
            erosion = graph.noise2d(shiftX, shiftZ, 0.25, erosionNoise);
            ridges = graph.noise2d(shiftX, shiftZ, 0.25, ridgeNoise);
            // The ridges spline is fed with RIDGES_FOLDED, which the vanilla preset
            // maps onto the same `minecraft:overworld/ridges` noise (folded = ridges).
            const int ridgesFolded = ridges;

            // The tunable relief field. Shifted and 0.25-scaled like the climate
            // noises so it is domain-warped by the same `offset` noise and its
            // hills bend around the continents instead of cutting across them.
            const int relief = graph.noise2d(shiftX, shiftZ, 0.25, reliefNoise);

            // The river field. Shifted and scaled exactly like the climate
            // noises so it is domain-warped by the same `offset` noise -- that
            // warp is what stops the contour network from reading as smooth
            // circular loops and gives rivers their bends.
            //
            // xzScale sets the period in WORLD blocks: the largest octave is
            // 2^6 cells wide, and a noise sampled at `x * xzScale` stretches that
            // to 2^6 / xzScale blocks (the same relation the relief noise above
            // documents for -5 and 0.25 -> 128 blocks), so riverWavelengthBlocks
            // is a literal period rather than an octave index to decode.
            const double riverScale = 64.0 / static_cast<double>(shape.riverWavelengthBlocks);
            river = graph.noise2d(shiftX, shiftZ, riverScale, riverNoise);

            // The river's own shape fields, shifted and scaled exactly like the
            // river field above so they wind along the same bends: a deep run
            // sits in the meander it was sampled in, not somewhere else on the
            // map. All four use the same period rule as the river field
            // (period = 2^6 / xzScale): 160-block depth runs, 24-block bed
            // roughness (the second depth sampler shares the depth noise at a
            // finer scale, the way jagged reuses its own), a 240-block
            // width breath from a separate noise so it stays decorrelated from
            // depth, and the fixed 768-block extent envelope that gives every
            // river its two ends. The second depth sampler shares the depth
            // noise at a finer scale -- one parameter set, two periods.
            riverDepthVar = graph.noise2d(shiftX, shiftZ, 64.0 / 160.0, riverDepthNoise);
            riverBedBump = graph.noise2d(shiftX, shiftZ, 64.0 / 24.0, riverDepthNoise);
            riverWidthVar = graph.noise2d(shiftX, shiftZ, 64.0 / 240.0, riverWidthNoise);
            riverExtentVar = graph.noise2d(shiftX, shiftZ, 64.0 / riverExtentWavelength,
                                            riverExtentNoise);

            graph.registerFunction("terrain/temperature", temperature);
            graph.registerFunction("terrain/vegetation", vegetation);
            graph.registerFunction("terrain/continentalness", continentalness);
            graph.registerFunction("terrain/erosion", erosion);
            graph.registerFunction("terrain/ridges", ridges);
            graph.registerFunction("terrain/ridges_folded", ridgesFolded);

            // ---- TerrainProvider splines -------------------------------
            const CubicSpline offsetSpline = buildOffsetSpline(continentalness, erosion, ridgesFolded, shape);
            const CubicSpline factorSpline = buildFactorSpline(continentalness, erosion, vegetation, ridges);
            const CubicSpline jaggednessSpline =
                buildJaggednessSpline(continentalness, erosion, vegetation, ridges);

            // splineWithBlending(s, target) = cache(lerp(blendAlpha, target, s)) and
            // blendAlpha is a ContextBoundSampler that reads 1.0 when no Blender is
            // bound, i.e. the plain spline. Same for blendOffset (0.0) and the
            // beardifier (0.0), so all three fold away.
            offset = graph.addConst(graph.spline(graph.addSpline(offsetSpline)), -0.50375f);

            // Relief rides on top of the spline offset. Negated because the
            // surface sits where depth == 0 and depth RISES with offset, so
            // adding a positive offset term would sink the terrain.
            const int reliefTerm = graph.mulConst(relief, -shape.reliefBlocks * offsetPerBlock);
            offset = graph.add(offset, reliefTerm);
            graph.registerFunction("kingscraft_relief", relief);
            graph.registerFunction("terrain/river", river);

            factor = graph.spline(graph.addSpline(factorSpline));
            depth = offsetToDepth(offset);

            const int unscaledJaggedness = graph.spline(graph.addSpline(jaggednessSpline));
            // NoiseRouterData uses DensityFunctions.noise(jagged, 1500.0, 0.0),
            // i.e. NoiseFunction(jagged, xzScale=1500, yScale=0, shift=0,0,0).
            // That is the UNSHIFTED sampler, so it is *not* the 0.25-scale
            // shiftedNoise2d form the climate noises use. The 1500 factor is
            // combined with the noise's own firstOctave=-16 (period 65536) to give
            // a ~43-block jaggedness wavelength.
            const int zero = graph.constant(0.0f);
            const int jaggedScaled = graph.noise2d(zero, zero, 1500.0, jaggedNoise);
            const int halfJagged = graph.halfNegative(jaggedScaled);
            const int jaggedness = graph.mul(unscaledJaggedness, halfJagged);
            graph.registerFunction("terrain/jaggedness", jaggedness);
            this->jaggedNoise = jaggedScaled;

            // initialDensity = noiseGradientDensity(factor, depth + jaggedness)
            const int unscaledDensity = graph.mul(graph.add(depth, jaggedness), factor);
            initialDensity = graph.mulConst(graph.quarterNegative(unscaledDensity), 4.0f);

            // slopedCheese = initialDensity + base_3d_noise
            slopedCheese = graph.add(initialDensity, graph.function("terrain/base_3d_noise"));
            graph.registerFunction("terrain/initial_density", initialDensity);
            graph.registerFunction("terrain/sloped_cheese", slopedCheese);
            graph.registerFunction("terrain/density", depth);
            graph.registerFunction("terrain/factor", factor);
            graph.registerFunction("terrain/offset", offset);

            // ---- final density ------------------------------------------
            // Vanilla: min(postProcess(slideDensity(caves), 4, 8), noodle) + beardifier
            // where caves = rangeChoice(slopedCheese, -1e6, 1.5625, surfaceWithEntrances,
            // underground(...)). Caves are deferred, so the rangeChoice collapses to
            // slopedCheese itself; the noodle/vein min and the beardifier add are the
            // ore/structure layer and are also skipped.
            finalDensity = postProcess(slideDensity(slopedCheese), 4, 8);
            graph.registerFunction("terrain/final_density", finalDensity);

            // ---- top surface -------------------------------------------
            preliminarySurfaceLevel = buildPreliminarySurfaceLevel(offset, factor);
            chunkSurfaceLevel = graph.interpolated(preliminarySurfaceLevel, 16, 1);
            surfaceNoise = addNoise(rootFactory, "surface");
            graph.registerFunction("terrain/preliminary_surface_level", preliminarySurfaceLevel);
            graph.registerFunction("terrain/chunk_surface_level", chunkSurfaceLevel);
            graph.registerFunction("terrain/surface_noise", surfaceNoise);

            // The graph is complete, so this must come LAST: computePurity()
            // sizes its per-node flags to the current node count, and eval()
            // reads those flags by node id. Calling it earlier would leave the
            // surface nodes above the end of the vector.
            //
            // Working out which nodes ignore y is what lets eval() memoise them
            // for a whole column instead of re-deriving them per y.
            graph.computePurity();
        }

        float sampleFinalDensity(int x, int y, int z) const { return graph.eval(finalDensity, x, y, z); }
        float sampleInitialDensity(int x, int y, int z) const { return graph.eval(initialDensity, x, y, z); }
        float sampleSlopedCheese(int x, int y, int z) const { return graph.eval(slopedCheese, x, y, z); }
        float sampleOffset(int x, int z) const { return graph.eval(offset, x, 0, z); }
        float sampleFactor(int x, int z) const { return graph.eval(factor, x, 0, z); }
        float sampleTemperature(int x, int z) const { return graph.eval(temperature, x, 0, z); }
        float sampleVegetation(int x, int z) const { return graph.eval(vegetation, x, 0, z); }
        float sampleContinentalness(int x, int z) const { return graph.eval(continentalness, x, 0, z); }
        float sampleErosion(int x, int z) const { return graph.eval(erosion, x, 0, z); }
        float sampleRidges(int x, int z) const { return graph.eval(ridges, x, 0, z); }
        float sampleChunkSurfaceLevel(int x, int z) const { return graph.eval(chunkSurfaceLevel, x, 0, z); }

        // Raw river field, before any width normalisation. Exposed for tooling
        // (tools/river_probe.cpp) that wants to look at the contour itself.
        float sampleRiver(int x, int z) const { return graph.eval(river, x, 0, z); }

        // Normalised river membership for one column, 0..1:
        //
        //   1.0  on the centreline
        //   >= riverChannelEdge()  inside the wetted channel
        //   > 0.0  somewhere in the bank blend
        //   0.0   outside the corridor entirely (and always 0 when rivers are off)
        //
        // The mask is a distance mask, not a raw noise value: it is built from
        // the distance to the contour in BLOCKS (|n| / |grad n|) divided by the
        // corridor half-width AT THIS COLUMN. A raw threshold on |n| would give
        // a channel whose width follows the noise's own gradient -- ballooning
        // wherever the field is flat, pinching wherever it is steep -- and that
        // width would then also shift with the wavelength dial, so
        // riverChannelHalfWidth could not mean "blocks" anywhere. Normalising
        // keeps "4.5 blocks" meaning 4.5 blocks, on average: the corridor
        // itself breathes 0.7x..1.3x with the width field (~240-block period)
        // and is scaled once more by the extent envelope (0 in the gaps
        // between rivers, 1 where one runs full width), so a river narrows,
        // widens and -- at its two ends -- tapers to nothing along its length
        // while every column still measures itself the same way. The
        // channel/bank split (riverChannelEdge_) is a RATIO of the two widths,
        // so it rides the same scaling for free and the wet channel keeps its
        // share of the corridor wherever it is.
        //
        // The gradient is a CENTRAL difference over ±riverStencil_ blocks, a
        // stencil as wide as the WIDEST corridor, and that width is the whole point.
        // |n|/|grad n| assumes the slope at this column is the slope all the way
        // to the contour; it is not. This field runs past hills, and on a hill
        // flank the one-block tangent can be ten times shallower than the mean
        // slope over the stretch that actually reaches the contour (measured: a
        // contour 16 blocks off estimated at 60) or twice as steep right beside
        // it (a contour 9 blocks off estimated at 4). Either error moves the
        // channel edge by blocks, so the river would pinch and balloon along its
        // own length. A stencil the width of the corridor measures the SECANT
        // slope -- the average over the walk the estimate is pretending to make
        // -- which is the quantity the formula actually needs, and central
        // rather than forward so the quadratic part cancels instead of biasing
        // the estimate to one side of the column.
        //
        // (x, z) is sampled FIRST on purpose: the graph memoises pure-2D nodes
        // per column, so that first eval hits whatever the column's own
        // evaluation already computed, and only the four stencil points retire
        // the cache. Those four retirements are what the next 2D consumer (the
        // climate tree) pays for; the alternative is a river of the wrong width.
        float sampleRiverMask(int x, int z) const {
            if (!riverEnabled_ || river < 0 || !(riverCorridorHalf_ > 0.0f)) return 0.0f;

            // Extent gate FIRST, before the gradient: in the gaps between
            // rivers this returns without sampling the four stencil points the
            // distance estimate needs, so the common case OUTSIDE gaps costs
            // one extra 2D eval and the case inside a gap costs less than it
            // used to. Evaluated at the column like the width field; the graph
            // memoises 2D nodes per column, so carveRiverTop paying for it
            // again on the same column is a cache hit.
            const float extent = riverExtentFactor(x, z);
            if (!(extent > 0.0f)) return 0.0f;

            const float n0 = graph.eval(river, x, 0, z);
            const float step = static_cast<float>(riverStencil_);
            const float gx = (graph.eval(river, x + riverStencil_, 0, z) -
                              graph.eval(river, x - riverStencil_, 0, z)) /
                             (2.0f * step);
            const float gz = (graph.eval(river, x, 0, z + riverStencil_) -
                              graph.eval(river, x, 0, z - riverStencil_)) /
                             (2.0f * step);
            const float grad = std::sqrt(gx * gx + gz * gz);
            // Flat field (or NaN): the linear approximation behind |n|/|grad|
            // collapses, and "no measurable contour" must not become "river",
            // so this fails closed to dry land.
            if (!(grad > 1.0e-6f)) return 0.0f;

            const float dist = std::fabs(n0) / grad;
            // Out of reach of even the widest this corridor can get. Returning
            // before the width noise is evaluated is the COMMON case -- most
            // columns are nowhere near a river -- so the width field costs
            // nothing on dry land or open ocean. Still valid with the extent
            // term: extent <= 1, so the scaled corridor never exceeds the
            // unscaled maximum this tests against.
            if (dist > riverCorridorMax_) return 0.0f;

            const float corridor =
                riverCorridorHalf_ * riverWidthFactor(x, z) * extent;
            if (dist > corridor) return 0.0f;

            const float mask = 1.0f - dist / corridor;
            return mask < 0.0f ? 0.0f : (mask > 1.0f ? 1.0f : mask);
        }

        // Corridor half-width at this column, as a multiple of the dial. The
        // width field is clamped to the swing on the way out rather than
        // trusting the noise's tail, so riverCorridorMax_ stays the true
        // maximum the distance test above relies on. Evaluated AT THE COLUMN:
        // the corridor is a property of where you are standing, and nothing in
        // the distance estimate depends on how the width varies along the way.
        float riverWidthFactor(int x, int z) const {
            const float w = 1.0f + riverWidthSwing * graph.eval(riverWidthVar, x, 0, z);
            return std::min(1.0f + riverWidthSwing, std::max(1.0f - riverWidthSwing, w));
        }

        // Extent envelope at this column: 0 in the gap where no river exists,
        // 1 where one runs full width, smoothstep in between so each end of a
        // river narrows AND shoals into dry land instead of stopping against
        // a wall. The mask scales the corridor by it (a dying river is a
        // creek) and carveRiverTop scales the bed depth by it (a dying river
        // is a SHALLOW creek), so by the time the factor reaches 0 the river
        // has already faded into the terrain over ~100-150 blocks.
        float riverExtentFactor(int x, int z) const {
            const float e = graph.eval(riverExtentVar, x, 0, z);
            float t = (e - riverExtentLo) / (riverExtentHi - riverExtentLo);
            t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
            return t * t * (3.0f - 2.0f * t);
        }

        // Carves one measured column top towards the river floor, in LOCAL y
        // (worldY - minY, the same space columnTop() answers in). Returns the
        // new top and never raises the ground.
        //
        // This lives here rather than in DefaultTerrainGenerator because the
        // biome selector needs the same answer: a column the carve put below sea
        // level has to come out RIVER, not OCEAN, and two implementations of
        // this arithmetic would eventually disagree about which columns those
        // are. It is a pure function of (x, z) and the top it is handed, so
        // neighbouring chunks always carve a shared column identically.
        //
        // Two cases fall through untouched: rivers switched off and an open
        // column (topLocal < 0). Submerged columns are carved too -- that is
        // the estuary, the trench continuing under the sea instead of stopping
        // dead at the natural coastline with a shelf-height sill beside the
        // mouth. min() is what keeps this safe over water: the channel branch
        // returns min(topLocal, profile), so a seabed already lower than the
        // profile (the open shelf, natural dips, the deep ocean) is left
        // exactly where it is and only shallower ground is trenched down to
        // the river floor. The bank branch cannot lower a submerged column at
        // all -- its blend runs from the top TOWARDS the waterline, which is
        // up, so min() picks the untouched top.
        int carveRiverTop(int x, int z, int topLocal, int seaLocal) const {
            if (!riverEnabled_ || topLocal < 0) return topLocal;

            const float mask = sampleRiverMask(x, z);
            if (!(mask > 0.0f)) return topLocal;

            // Channel: a U-shaped bed rather than a flat shelf. t runs from 0 at
            // the channel edge to 1 on the centreline (both are distances from
            // the same contour, so the ratio survives the width field scaling
            // with it), and f(t) = 2t - t^2 is the cross-section: steep where
            // the shore meets the water, flat along the bottom. The bed is
            //
            //     sea - extent(x,z) * max(depth(x,z) + roughness(x,z), 1) * f(t)
            //
            // where depth is the dial scaled 0.45x..1.55x by a ~160-block noise
            // (long deep runs and shallow pools along the river), roughness is
            // a +-0.75-block ~24-block wobble on the bottom, and extent is the
            // fixed envelope that gives every river its two ends: it multiplies
            // the whole depth, so the last stretch of a river is a shallow
            // creek before it is nothing. depth and roughness are multiplied by
            // f(t) as well, so they vanish exactly at the channel edge: the bed
            // meets the bank AT the waterline, continuous, with no wall and no
            // shelf -- and the wet width stays a distance (the channel width)
            // instead of becoming a function of how the depth rounds. Where the
            // terrain under the channel is already lower than the profile,
            // min() keeps it: natural dips survive the carve.
            //
            // bank == 0 collapses the corridor into the channel and leaves
            // riverChannelEdge_ == 0; the same formula gives t = mask there, so
            // the all-channel case needs no branch of its own.
            if (riverChannelEdge_ <= 0.0f || mask >= riverChannelEdge_) {
                const float t = (mask - riverChannelEdge_) / (1.0f - riverChannelEdge_);
                const float f = t * (2.0f - t);
                const float vary =
                    1.0f + riverDepthSwing * graph.eval(riverDepthVar, x, 0, z);
                float total =
                    riverDepth_ * vary + riverBumpAmp * graph.eval(riverBedBump, x, 0, z);
                if (total < 1.0f) total = 1.0f; // full-width rivers hold a block of water
                // Extent LAST, after the clamp: the clamp is the guarantee for
                // a full-width river, and the envelope deliberately takes the
                // water away again at the tips -- total < 1 there is what
                // shoals the end out to dry ground instead of keeping a
                // one-block slot in the terrain. At the tip the rounded bed
                // lands ON the waterline, so the sea-level fill finds nothing
                // under sea level and the river is simply over.
                total *= riverExtentFactor(x, z);
                const float bedY =
                    std::max(0.0f, static_cast<float>(seaLocal) - total * f);
                return std::min(topLocal, static_cast<int>(std::lround(bedY)));
            }

            // Bank: smoothstep from the WATERLINE at the channel edge back to
            // the untouched height at the corridor edge. The two ends agree with
            // their neighbours (waterline at the edge, topLocal beyond the
            // corridor) so the land profile is continuous in x/z.
            //
            // The inner end is the waterline, and that is load-bearing. Blending
            // up from some other height would leave part of the bank below sea
            // level as well, so the WET width would stop being a distance and
            // start being a function of how high the ground happened to be: flat
            // land near sea level would come out with a river twice
            // riverChannelHalfWidth wide, steep land with a sliver. Ending the
            // bank exactly at the waterline makes riverChannelHalfWidth the
            // width of the water and riverBankWidth the width of dry slope,
            // whatever the terrain underneath was doing -- and it is the same
            // point the channel profile starts from, so bank and bed meet
            // without a step.
            const float bankY = static_cast<float>(seaLocal);
            const float t = mask / riverChannelEdge_;
            const float s = t * t * (3.0f - 2.0f * t);
            const float blended = static_cast<float>(topLocal) + (bankY - static_cast<float>(topLocal)) * s;
            return std::min(topLocal, static_cast<int>(std::lround(blended)));
        }

        bool riversEnabled() const { return riverEnabled_; }
        // Mask value at the channel edge; below this a column is bank, above it
        // a column is channel.
        float riverChannelEdge() const { return riverChannelEdge_; }
        // Typical channel depth, in blocks below sea level. The local bed
        // varies 0.45x..1.55x this along the river and is scaled to zero at
        // each end by the fixed extent envelope; see carveRiverTop.
        float riverDepthBelowSea() const { return riverDepth_; }
        // Stencil width, in blocks, of the secant gradient the mask measures.
        int riverStencil() const { return riverStencil_; }

        // SurfaceRules.StoneDepthRule samples minecraft:surface, and the
        // underground bands in material_rule/overworld/surface.json threshold the
        // same noise. Sampled as a plain 2D field.
        float sampleSurfaceNoise(int x, int z) const { return graph.eval(surfaceNoise, x, 0, z); }

        // NoiseRouterData.preliminarySurfaceLevel's upperBound: the highest y at
        // which terrain can possibly be solid. The generator starts its top-down
        // scan here instead of at the top of the world.
        float sampleSurfaceLevelUpperBound(int x, int z) const {
            return graph.eval(surfaceUpperBound, x, 0, z);
        }

    private:
        // River dials baked at build time; see buildFromFactory.
        //
        // The swings below are fixed design constants, not dials: they are the
        // shape of the natural variation, while the dials in TerrainShape stay
        // the single "how big is this world's river" knob. The extent envelope
        // is a constant for the same reason -- river LENGTH is a generator
        // property, not a world dial (no TerrainShape field, no metadata tag,
        // GENERATOR_VERSION stays put).
        static constexpr float riverWidthSwing = 0.3f;   // corridor 0.7x..1.3x
        static constexpr float riverDepthSwing = 0.55f;  // depth    0.45x..1.55x
        static constexpr float riverBumpAmp = 0.75f;     // bed roughness, blocks
        // Extent envelope: period sets how far a river can run before the
        // contour crosses into a gap (rivers come out ~300-800 block arcs at
        // 768), and the lo/hi band sets where the taper starts -- 0.4 noise
        // units of ramp over ~190 blocks, so each end fades over roughly
        // 100-150 blocks of narrowing, shoaling creek.
        static constexpr float riverExtentWavelength = 768.0f;
        static constexpr float riverExtentLo = -0.05f;  // factor reaches 0 here
        static constexpr float riverExtentHi = 0.35f;   // factor reaches 1 here
        bool riverEnabled_ = false;
        float riverCorridorHalf_ = 0.0f;
        float riverCorridorMax_ = 0.0f;
        float riverChannelEdge_ = 0.0f;
        float riverDepth_ = 5.0f;
        int riverStencil_ = 4;

        int addNoise(const PositionalRandomFactory& rootFactory, const std::string& id) {
            const std::unique_ptr<RandomSource> seeded = rootFactory.fromHashOf("minecraft:" + id);
            NormalNoise noise(parameters(id));
            noise.create(*seeded);
            return graph.addNoise(std::move(noise));
        }

        // NoiseRouterData.offsetToDepth(offset) = yClampedGradient(-64, 320, 1.5, -1.5) + offset
        int offsetToDepth(int offsetFn) {
            return graph.add(graph.yLinearGradient(minY, maxY, 1.5f, -1.5f), offsetFn);
        }

        int noiseGradientDensity(int factorFn, int depthWithJaggedness) {
            return graph.mulConst(graph.quarterNegative(graph.mul(depthWithJaggedness, factorFn)), 4.0f);
        }

        // NoiseRouterData.postProcess(slide, 4, 8)
        //   = interpolated(blendDensity(slide) * 0.64, cellSizeXz, cellSizeY)
        // blendDensity is the identity without a Blender, and squeeze() is an
        // optimisation hint with no effect on values.
        int postProcess(int slide, int cellSizeXz, int cellSizeY) {
            return graph.interpolated(graph.mulConst(slide, 0.64f), cellSizeXz, cellSizeY);
        }

        // slideDensity(amplified=false, caves):
        //   slide(caves, -64, 384, 80, 64, -0.078125, 0, 24, 0.1171875)
        int slideDensity(int caves) {
            const int topStartY = minY + height - 80;  // 240
            const int topEndY = minY + height - 64;    // 256
            const int bottomStartY = minY + 0;         // -64
            const int bottomEndY = minY + 24;          // -40

            // NoiseRouterData.slide:
            //   noiseValue   = lerp(topFactor, topTarget, caves)
            //   result       = lerp(bottomFactor, bottomTarget, noiseValue)
            // The blend target is the lerp's FIRST argument, so at factor 0 the
            // result is the target and at factor 1 it is the noise.
            const int topFactor = graph.yLinearGradient(topStartY, topEndY, 1.0f, 0.0f);
            const int noiseValue = graph.lerpConstFirst(topFactor, -0.078125f, caves);
            const int bottomFactor = graph.yLinearGradient(bottomStartY, bottomEndY, 0.0f, 1.0f);
            return graph.lerpConstFirst(bottomFactor, 0.1171875f, noiseValue);
        }

        // NoiseRouterData.preliminarySurfaceLevel(offset, factor, amplified=false):
        //   upperBound = clamp(remap(0.2734375 / factor - offset, 1.5, -1.5, -64, 320), -40, 320)
        //   density    = findTopSurface(
        //                    slideDensity(clamp(noiseGradientDensity(factor, depth) - 0.703125, -64, 64))
        //                                - 0.390625,
        //                    upperBound, -64, 8)
        int buildPreliminarySurfaceLevel(int offsetFn, int factorFn) {
            // DensityFunctions.div(constant(0.2734375F), factor), not a multiply.
            const int quotient = graph.div(graph.constant(0.2734375f), factorFn);
            const int upperBound =
                graph.clamp(graph.remap(graph.sub(quotient, offsetFn), 1.5f, -1.5f, -64.0f, 320.0f), -40.0f,
                            320.0f);

            const int gradient = noiseGradientDensity(factorFn, offsetToDepth(offsetFn));
            const int clamped = graph.clamp(graph.addConst(gradient, -0.703125f), -64.0f, 64.0f);
            const int density = graph.addConst(slideDensity(clamped), -0.390625f);

            surfaceUpperBound = upperBound;
            return graph.findTopSurface(density, upperBound, minY, 8);
        }

        // ---- TerrainProvider overworldOffset ---------------------------
        //
        // Only the inland branches (mid/high) take shape dials. beach and low are
        // the coastline and shallow shelf, whose narrow height range is what makes
        // beaches and ocean floors behave; widening those would flood the coasts.
        static CubicSpline buildOffsetSpline(int continents, int erosion, int ridges,
                                              const kc::TerrainShape& shape) {
            const CubicSpline beach = buildErosionOffsetSpline(erosion, ridges, -0.15f, 0.0f, 0.0f, 0.1f,
                                                              0.0f, -0.03f, false, false);
            const CubicSpline low = buildErosionOffsetSpline(erosion, ridges, -0.1f, 0.03f, 0.1f, 0.1f,
                                                             0.01f, -0.03f, false, false);
            // mountainFactor (the 6th argument) is left at the vanilla 0.7 / 1.0:
            // it scales the MOUNTAIN splines, not the lowlands, so it must not
            // follow the lowland dials or dialling up the plains would also
            // inflate every peak.
            const CubicSpline mid = buildErosionOffsetSpline(erosion, ridges, -0.1f, shape.lowlandHill,
                                                             shape.lowlandTall, 0.7f, shape.lowlandPlain,
                                                             -0.03f, true, true);
            const CubicSpline high = buildErosionOffsetSpline(erosion, ridges, -0.05f, shape.lowlandHill,
                                                              shape.lowlandTall, 1.0f, shape.lowlandPlain,
                                                              0.01f, true, true);

            CubicSplineBuilder b(continents, SplineValueTransform::Identity);
            b.addPoint(-1.1f, 0.044f);
            b.addPoint(-1.02f, -0.2222f);
            b.addPoint(-0.51f, -0.2222f);
            b.addPoint(-0.44f, -0.12f);
            b.addPoint(-0.18f, -0.12f);
            b.addNestedPoint(-0.16f, beach);
            b.addNestedPoint(-0.15f, beach);
            b.addNestedPoint(-0.1f, low);
            b.addNestedPoint(0.25f, mid);
            b.addNestedPoint(1.0f, high);
            return b.build();
        }

        // ---- TerrainProvider overworldFactor ---------------------------
        static CubicSpline buildFactorSpline(int continents, int erosion, int weirdness, int ridges) {
            CubicSplineBuilder b(continents, SplineValueTransform::Identity);
            b.addPoint(-0.19f, 3.95f);
            b.addNestedPoint(-0.15f, erosionFactor(erosion, weirdness, ridges, 6.25f, true));
            b.addNestedPoint(-0.1f, erosionFactor(erosion, weirdness, ridges, 5.47f, true));
            b.addNestedPoint(0.03f, erosionFactor(erosion, weirdness, ridges, 5.08f, true));
            b.addNestedPoint(0.06f, erosionFactor(erosion, weirdness, ridges, 4.69f, false));
            return b.build();
        }

        static CubicSpline erosionFactor(int erosion, int weirdness, int ridges, float baseValue,
                                         bool shatteredTerrain) {
            const CubicSpline baseSpline = weirdnessSpline(weirdness, -0.2f, 6.3f, 0.2f, baseValue);

            CubicSplineBuilder b(erosion, SplineValueTransform::Identity);
            b.addNestedPoint(-0.6f, baseSpline);
            b.addNestedPoint(-0.5f, weirdnessSpline(weirdness, -0.05f, 6.3f, 0.05f, 2.67f));
            b.addNestedPoint(-0.35f, baseSpline);
            b.addNestedPoint(-0.25f, baseSpline);
            b.addNestedPoint(-0.1f, weirdnessSpline(weirdness, -0.05f, 2.67f, 0.05f, 6.3f));
            b.addNestedPoint(0.03f, baseSpline);

            if (shatteredTerrain) {
                const CubicSpline weirdnessShattered = weirdnessSpline(weirdness, 0.0f, baseValue, 0.1f, 0.625f);
                CubicSplineBuilder rb(ridges, SplineValueTransform::Identity);
                rb.addPoint(-0.9f, baseValue);
                rb.addNestedPoint(-0.69f, weirdnessShattered);
                const CubicSpline ridgesShattered = rb.build();

                b.addPoint(0.35f, baseValue);
                b.addNestedPoint(0.45f, ridgesShattered);
                b.addNestedPoint(0.55f, ridgesShattered);
                b.addPoint(0.62f, baseValue);
            } else {
                CubicSplineBuilder e(ridges, SplineValueTransform::Identity);
                e.addNestedPoint(-0.7f, baseSpline);
                e.addPoint(-0.15f, 1.37f);
                const CubicSpline extremeHills = e.build();

                CubicSplineBuilder p(ridges, SplineValueTransform::Identity);
                p.addNestedPoint(0.45f, baseSpline);
                p.addPoint(0.7f, 1.56f);
                const CubicSpline extra3d = p.build();

                b.addNestedPoint(0.05f, extra3d);
                b.addNestedPoint(0.4f, extra3d);
                b.addNestedPoint(0.45f, extremeHills);
                b.addNestedPoint(0.55f, extremeHills);
                b.addPoint(0.58f, baseValue);
            }
            return b.build();
        }

        static CubicSpline weirdnessSpline(int weirdness, float loc1, float val1, float loc2, float val2) {
            CubicSplineBuilder b(weirdness, SplineValueTransform::Identity);
            b.addPoint(loc1, val1);
            b.addPoint(loc2, val2);
            return b.build();
        }

        // ---- TerrainProvider overworldJaggedness -----------------------
        static CubicSpline buildJaggednessSpline(int continents, int erosion, int weirdness, int ridges) {
            const CubicSpline at03 = erosionJaggedness(erosion, weirdness, ridges, 1.0f, 0.5f, 0.0f, 0.0f);
            const CubicSpline at065 = erosionJaggedness(erosion, weirdness, ridges, 1.0f, 1.0f, 1.0f, 0.0f);

            CubicSplineBuilder b(continents, SplineValueTransform::Identity);
            b.addPoint(-0.11f, 0.0f);
            b.addNestedPoint(0.03f, at03);
            b.addNestedPoint(0.65f, at065);
            return b.build();
        }

        static CubicSpline erosionJaggedness(int erosion, int weirdness, int ridges,
                                              float peakRidgeErosion0, float peakRidgeErosion1,
                                              float highRidgeErosion0, float highRidgeErosion1) {
            const CubicSpline atErosion0 =
                ridgeJaggedness(weirdness, ridges, peakRidgeErosion0, highRidgeErosion0);
            const CubicSpline atErosion1 =
                ridgeJaggedness(weirdness, ridges, peakRidgeErosion1, highRidgeErosion1);

            CubicSplineBuilder b(erosion, SplineValueTransform::Identity);
            b.addNestedPoint(-1.0f, atErosion0);
            b.addNestedPoint(-0.78f, atErosion1);
            b.addNestedPoint(-0.5775f, atErosion1);
            b.addPoint(-0.375f, 0.0f);
            return b.build();
        }

        static float peaksAndValleys(float weirdness) {
            return -(std::fabs(std::fabs(weirdness) - 0.6666667f) - 0.33333334f) * 3.0f;
        }

        static CubicSpline ridgeJaggedness(int weirdness, int ridges, float peakRidge, float highRidge) {
            const float highSliceStart = peaksAndValleys(0.4f);
            const float highSliceEnd = peaksAndValleys(0.56666666f);
            const float highSliceMiddle = (highSliceStart + highSliceEnd) / 2.0f;

            CubicSplineBuilder b(ridges, SplineValueTransform::Identity);
            b.addPoint(highSliceStart, 0.0f);
            if (highRidge > 0.0f) {
                b.addNestedPoint(highSliceMiddle, weirdnessJaggedness(weirdness, highRidge));
            } else {
                b.addPoint(highSliceMiddle, 0.0f);
            }
            if (peakRidge > 0.0f) {
                b.addNestedPoint(1.0f, weirdnessJaggedness(weirdness, peakRidge));
            } else {
                b.addPoint(1.0f, 0.0f);
            }
            return b.build();
        }

        static CubicSpline weirdnessJaggedness(int weirdness, float jaggednessFactor) {
            CubicSplineBuilder b(weirdness, SplineValueTransform::Identity);
            b.addPoint(-0.01f, 0.63f * jaggednessFactor);
            b.addPoint(0.01f, 0.3f * jaggednessFactor);
            return b.build();
        }

        // ---- TerrainProvider buildErosionOffsetSpline -------------------
        static CubicSpline buildErosionOffsetSpline(int erosion, int ridges, float lowValley, float hill,
                                                    float tallHill, float mountainFactor, float plain,
                                                    float swamp, bool includeExtremeHills, bool saddle) {
            const CubicSpline veryLowErosionMountains = mountainRidgeSpline(
                ridges, lerp(mountainFactor, 0.6f, 1.5f), saddle);
            const CubicSpline lowErosionMountains =
                mountainRidgeSpline(ridges, lerp(mountainFactor, 0.6f, 1.0f), saddle);
            const CubicSpline mountains = mountainRidgeSpline(ridges, mountainFactor, saddle);
            const CubicSpline widePlateau =
                ridgeSpline(ridges, lowValley - 0.15f, 0.5f * mountainFactor,
                            // Vanilla writes MathHelper.lerp(0.5, 0.5, 0.5) here,
                            // which is 0.5 + 0.5 * (0.5 - 0.5) == 0.5, so it is
                            // the same knot as the one below it. Spelled out to
                            // match rather than left as a no-op call.
                            0.5f * mountainFactor, 0.5f * mountainFactor,
                            0.6f * mountainFactor, 0.5f);
            const CubicSpline narrowPlateau =
                ridgeSpline(ridges, lowValley, plain * mountainFactor, hill * mountainFactor,
                            0.5f * mountainFactor, 0.6f * mountainFactor, 0.5f);
            const CubicSpline plains = ridgeSpline(ridges, lowValley, plain, plain, hill, tallHill, 0.5f);
            const CubicSpline plainsFarInland =
                ridgeSpline(ridges, lowValley, plain, plain, hill, tallHill, 0.5f);

            CubicSplineBuilder extreme(ridges, SplineValueTransform::Identity);
            extreme.addPoint(-1.0f, lowValley);
            extreme.addNestedPoint(-0.4f, plains);
            extreme.addPoint(0.0f, tallHill + 0.07f);
            const CubicSpline extremeHills = extreme.build();

            const CubicSpline swamps =
                ridgeSpline(ridges, -0.02f, swamp, swamp, hill, tallHill, 0.0f);

            CubicSplineBuilder b(erosion, SplineValueTransform::Identity);
            b.addNestedPoint(-0.85f, veryLowErosionMountains);
            b.addNestedPoint(-0.7f, lowErosionMountains);
            b.addNestedPoint(-0.4f, mountains);
            b.addNestedPoint(-0.35f, widePlateau);
            b.addNestedPoint(-0.1f, narrowPlateau);
            b.addNestedPoint(0.2f, plains);
            if (includeExtremeHills) {
                b.addNestedPoint(0.4f, plainsFarInland);
                b.addNestedPoint(0.45f, extremeHills);
                b.addNestedPoint(0.55f, extremeHills);
                b.addNestedPoint(0.58f, plainsFarInland);
            }
            b.addNestedPoint(0.7f, swamps);
            return b.build();
        }

        static float lerp(float alpha, float p0, float p1) { return p0 + alpha * (p1 - p0); }

        // TerrainProvider.mountainContinentalness
        static float mountainContinentalness(float ridge, float modulation) {
            constexpr float ridgeOffset = 1.17f;
            constexpr float ridgeAmplitude = 0.46082947f;
            const float ridgeSlope = 1.0f - (1.0f - modulation) * 0.5f;
            const float ridgeIntersect = 0.5f * (1.0f - modulation);
            const float adjustedRidgeHeight = (ridge + ridgeOffset) * ridgeAmplitude;
            const float continentalness = adjustedRidgeHeight * ridgeSlope - ridgeIntersect;
            return ridge < -0.7f ? std::fmax(continentalness, -0.2222f) : std::fmax(continentalness, 0.0f);
        }

        // TerrainProvider.calculateMountainRidgeZeroContinentalnessPoint
        static float ridgeZeroContinentalnessPoint(float modulation) {
            constexpr float ridgeOffset = 1.17f;
            constexpr float ridgeAmplitude = 0.46082947f;
            const float ridgeSlope = 1.0f - (1.0f - modulation) * 0.5f;
            const float ridgeIntersect = 0.5f * (1.0f - modulation);
            return ridgeIntersect / (ridgeAmplitude * ridgeSlope) - ridgeOffset;
        }

        // TerrainProvider.buildMountainRidgeSplineWithPoints
        static CubicSpline mountainRidgeSpline(int ridges, float modulation, bool saddle) {
            const float minPointContinentalness = mountainContinentalness(-1.0f, modulation);
            const float maxPointContinentalness = mountainContinentalness(1.0f, modulation);
            const float ridgeZeroPoint = ridgeZeroContinentalnessPoint(modulation);

            CubicSplineBuilder b(ridges, SplineValueTransform::Identity);
            if (-0.65f < ridgeZeroPoint && ridgeZeroPoint < 1.0f) {
                const float afterRiverThreshold = mountainContinentalness(-0.65f, modulation);
                const float beforeRiverThreshold = mountainContinentalness(-0.75f, modulation);
                const float minPointDerivative = slope(minPointContinentalness, beforeRiverThreshold, -1.0f, -0.75f);
                b.addPoint(-1.0f, minPointContinentalness, minPointDerivative);
                b.addPoint(-0.75f, beforeRiverThreshold);
                b.addPoint(-0.65f, afterRiverThreshold);
                const float ridgeZeroContinentalness = mountainContinentalness(ridgeZeroPoint, modulation);
                const float maxPointDerivative = slope(ridgeZeroContinentalness, maxPointContinentalness, ridgeZeroPoint, 1.0f);
                constexpr float smallOffset = 0.01f;
                b.addPoint(ridgeZeroPoint - smallOffset, ridgeZeroContinentalness);
                b.addPoint(ridgeZeroPoint, ridgeZeroContinentalness, maxPointDerivative);
                b.addPoint(1.0f, maxPointContinentalness, maxPointDerivative);
            } else {
                const float simpleDerivative = slope(minPointContinentalness, maxPointContinentalness, -1.0f, 1.0f);
                if (saddle) {
                    b.addPoint(-1.0f, std::fmax(0.2f, minPointContinentalness));
                    b.addPoint(0.0f, lerp(0.5f, minPointContinentalness, maxPointContinentalness), simpleDerivative);
                } else {
                    b.addPoint(-1.0f, minPointContinentalness, simpleDerivative);
                }
                b.addPoint(1.0f, maxPointContinentalness, simpleDerivative);
            }
            return b.build();
        }

        static float slope(float y1, float y2, float x1, float x2) { return (y2 - y1) / (x2 - x1); }

        // TerrainProvider.ridgeSpline
        static CubicSpline ridgeSpline(int ridges, float valley, float low, float mid, float high,
                                       float peaks, float minValleySteepness) {
            const float d1 = std::fmax(0.5f * (low - valley), minValleySteepness);
            const float d2 = 5.0f * (mid - low);

            CubicSplineBuilder b(ridges, SplineValueTransform::Identity);
            b.addPoint(-1.0f, valley, d1);
            b.addPoint(-0.4f, low, std::fmin(d1, d2));
            b.addPoint(0.0f, mid, d2);
            b.addPoint(0.4f, high, 2.0f * (high - mid));
            b.addPoint(1.0f, peaks, 0.7f * (peaks - high));
            return b.build();
        }
    };

}
