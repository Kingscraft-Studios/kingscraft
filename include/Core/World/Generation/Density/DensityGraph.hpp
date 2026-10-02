#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/World/Generation/Noise/BlendedNoise.hpp"
#include "CubicSpline.hpp"
#include "Core/World/Generation/Noise/NormalNoise.hpp"

namespace kc {

    // A flat, index-based density-function graph.
    //
    // Vanilla builds a deeply nested tree of record types and evaluates it through
    // virtual dispatch. Terrain is evaluated ~98k times per chunk (16x384x16) and the
    // graph is re-evaluated per Y band, so this keeps the same node semantics but
    // stores every operation in one vector with integer child indices. That makes the
    // graph buildable once and evaluable without a per-sample allocator.
    //
    // Node semantics are ports of the vanilla functions actually reachable from the
    // no-cave overworld router. The four always-constant vanilla functions
    // (blendAlpha, blendOffset, beardifier, blendDensity) are folded away: with no
    // Blender in scope blendDensity is the identity and the other three are 0.
    class DensityGraph {
    public:

        // Per-sample memoisation. The Overworld graph is a DAG in which the same
        // spline/noise sub-expression is reachable by many paths from the root,
        // so a plain recursive walk re-evaluates it exponentially often (measurably
        // ~58us per density sample, ~5.7s per 16x16x384 chunk). Vanilla has the
        // same DAG and the same fix: DensityGraph.sampleWithCache keeps a
        // float-per-node scratch array valid for one (x,y,z) sample.
        //
        // The cache is thread_local and sized to nodes_.size(), because several
        // chunk threads may evaluate the same graph concurrently.
        // Memoisation state. Two caches are kept because the Overworld graph
        // mixes two very different kinds of node:
        //
        //  * "pure 2D" nodes (climate noises, shift noises, and every spline
        //    built from them) do not read y at all. Their value depends only on
        //    (x, z), so keying them on x,y,z would recompute the whole spline
        //    tree for every one of the 384 y values in a column. Keying them on
        //    (x, z) makes them evaluate once per column instead.
        //  * everything else is keyed on the full (x, y, z) sample point.
        //
        // The coordinate check is what makes the whole thing correct:
        // interpolated() deliberately samples its inner function at four
        // different columns, so any coordinate change retires that cache.
        //
        // Vanilla has the same DAG and the same problem, and solves it the same
        // way in DensityGraph.sampleWithCache (a float-per-node scratch array
        // valid for one sample). Without this a chunk costs ~5.7s.
        // The eight corner samples of a single interpolated() cell. Cached per
        // cell so the eight samples that share a cell evaluate the expensive
        // inner function once, not eight times.
        struct Cell {
            int x = 0, y = 0, z = 0;
            float v[8] = {};
            bool valid = false;
        };

        struct SampleCache {
            // The cache is a function-local thread_local, i.e. one per thread for
            // the whole process, so it outlives any individual graph. Two graphs
            // (e.g. two OverworldNoise built with different seeds) can hold the
            // same node count and be sampled at the same coordinates, and the
            // coordinate/epoch check alone would then happily return the *other*
            // graph's values. `owner` is each graph's address, so a mismatch
            // invalidates everything.
            const void* owner = nullptr;
            std::vector<float> values2d, values3d;
            std::vector<uint32_t> stamps2d, stamps3d;
            uint32_t epoch2d = 0, epoch3d = 0; // 0 == "no sample in progress"
            int x2 = 0, z2 = 0;
            int x3 = 0, y3 = 0, z3 = 0;
            // CacheAllInCell: the 8 corners of one interpolated() cell.
            std::vector<Cell> cells;
        };

        enum class Kind {
            Constant,
            Noise2d,   // shiftedNoise2d(shiftX, shiftZ, xzScale, noise)
            Noise3d,   // noise(noise, xzScale, yScale, shiftX, shiftZ)
            ShiftA,    // shiftA(noise)
            ShiftB,    // shiftB(noise)
            Blended,   // BlendedNoise
            Add,
            Sub,
            Mul,
            Div,
            Min,
            Max,
            Abs,
            Clamp,
            // 26.3 implements half_negative and quarter_negative as
            // UnaryFunction.LeakyReLUSampler: `v > 0 ? v : v * factor`. This is NOT the
            // older modular sawtooth `x - floor(x / n) * n`; getting it wrong turns the
            // terrain density gradient into a discontinuous sawtooth.
            HalfNegative,
            QuarterNegative,
            Lerp3,     // lerp(alpha, first, second) with three child functions
            LerpConst,
            LerpConstFirst, // lerp(alpha, constValue, second): target + alpha*(second-target)
            YLinearGradient,
            Interpolated,
            Spline,
            FindTopSurface,
        };

        struct Node {
            Kind kind = Kind::Constant;
            float c0 = 0.0f, c1 = 0.0f, c2 = 0.0f, c3 = 0.0f;
            // Noise scales are double in vanilla (NoiseFunction takes double xzScale /
            // yScale). `blockX + shiftX` is a float, but multiplying that by a double
            // scale widens to double BEFORE the multiply. Doing the multiply in float
            // instead costs about 1e-3 in the final density, which is visible in the
            // terrain shape, so d0/d1 must stay double.
            double d0 = 0.0, d1 = 0.0;
            int a = -1, b = -1, c = -1;
            int noise = -1;
            int spline = -1;
            int cellSizeXz = 0;
            int cellSizeY = 0;
            int lowerBound = 0;
            int toY = 0;
        };

        // ---- construction -------------------------------------------------

        int constant(float value) {
            Node n;
            n.kind = Kind::Constant;
            n.c0 = value;
            return push(n);
        }

        int zero() { return constant(0.0f); }

        int noise2d(int shiftX, int shiftZ, double xzScale, int noiseIndex) {
            Node n;
            n.kind = Kind::Noise2d;
            n.a = shiftX;
            n.b = shiftZ;
            n.d0 = xzScale;
            n.noise = noiseIndex;
            return push(n);
        }

        int noise3d(int noiseIndex, double xzScale, double yScale, int shiftX = -1, int shiftZ = -1) {
            Node n;
            n.kind = Kind::Noise3d;
            n.noise = noiseIndex;
            n.d0 = xzScale;
            n.d1 = yScale;
            n.a = shiftX;
            n.b = shiftZ;
            return push(n);
        }

        int shiftA(int noiseIndex) {
            Node n;
            n.kind = Kind::ShiftA;
            n.noise = noiseIndex;
            return push(n);
        }

        int shiftB(int noiseIndex) {
            Node n;
            n.kind = Kind::ShiftB;
            n.noise = noiseIndex;
            return push(n);
        }

        int blended(int index) {
            Node n;
            n.kind = Kind::Blended;
            n.noise = index;
            return push(n);
        }

        int add(int a, int b) { return binary(Kind::Add, a, b); }
        int sub(int a, int b) { return binary(Kind::Sub, a, b); }
        int mul(int a, int b) { return binary(Kind::Mul, a, b); }
        int div(int a, int b) { return binary(Kind::Div, a, b); }
        int min(int a, int b) { return binary(Kind::Min, a, b); }
        int max(int a, int b) { return binary(Kind::Max, a, b); }

        int abs(int a) { return unary(Kind::Abs, a); }
        int halfNegative(int a) { return unary(Kind::HalfNegative, a); }
        int quarterNegative(int a) { return unary(Kind::QuarterNegative, a); }
        int clamp(int a, float lo, float hi) {
            Node n;
            n.kind = Kind::Clamp;
            n.a = a;
            n.c0 = lo;
            n.c1 = hi;
            return push(n);
        }
        int mulConst(int a, float value) { return mul(a, constant(value)); }
        int addConst(int a, float value) { return add(a, constant(value)); }

        int lerp3(int alpha, int first, int second) {
            Node n;
            n.kind = Kind::Lerp3;
            n.a = alpha;
            n.b = first;
            n.c = second;
            return push(n);
        }

        // LerpFunction(alpha, first, second) with `first` a constant:
        //   Mth.lerp(alpha, first, second) = first + alpha * (second - first)
        // The constant is the lerp's FIRST argument, which is the opposite of
        // lerpConstSecond; getting this backwards silently flattens the terrain.
        int lerpConstFirst(int alpha, float first, int second) {
            Node n;
            n.kind = Kind::LerpConstFirst;
            n.a = alpha;
            n.b = second;
            n.c0 = first;
            return push(n);
        }

        int lerpConstSecond(int alpha, int first, float second) {
            Node n;
            n.kind = Kind::LerpConst;
            n.a = alpha;
            n.b = first;
            n.c0 = second;
            return push(n);
        }

        // GradientFunction(Y, CLAMP_TO_EDGE, fromY, toY, fromValue, toValue):
        // a clamped linear ramp along Y.
        int yLinearGradient(int fromY, int toY, float fromValue, float toValue) {
            Node n;
            n.kind = Kind::YLinearGradient;
            n.c0 = fromValue;
            n.c1 = toValue;
            n.c2 = (toValue - fromValue) / static_cast<float>(toY - fromY);
            n.lowerBound = fromY;
            n.toY = toY;
            return push(n);
        }

        int interpolated(int input, int cellSizeXz, int cellSizeY) {
            Node n;
            n.kind = Kind::Interpolated;
            n.a = input;
            n.cellSizeXz = cellSizeXz;
            n.cellSizeY = cellSizeY;
            return push(n);
        }

        int spline(int splineIndex) {
            Node n;
            n.kind = Kind::Spline;
            n.spline = splineIndex;
            return push(n);
        }

        int findTopSurface(int density, int upperBound, int lowerBound, int cellHeight) {
            Node n;
            n.kind = Kind::FindTopSurface;
            n.a = density;
            n.b = upperBound;
            n.lowerBound = lowerBound;
            n.cellSizeY = cellHeight;
            return push(n);
        }

        // DensityFunctions.remap(x, fromMin, fromMax, toMin, toMax) is a multiply and
        // an add; vanilla literally rewrites it into those two, so do the same and
        // skip the special case where the offset is zero.
        int remap(int input, float fromMin, float fromMax, float toMin, float toMax) {
            const float factor = (toMax - toMin) / (fromMax - fromMin);
            const float offset = toMin - fromMin * factor;
            const int scaled = mulConst(input, factor);
            return offset == 0.0f ? scaled : addConst(scaled, offset);
        }

        // ---- tables -------------------------------------------------------

        int addNoise(NormalNoise noise) {
            noises_.push_back(std::move(noise));
            return static_cast<int>(noises_.size()) - 1;
        }
        int addBlended(BlendedNoise noise) {
            blended_.push_back(std::move(noise));
            return static_cast<int>(blended_.size()) - 1;
        }
        int addSpline(CubicSpline s) {
            splines_.push_back(std::move(s));
            return static_cast<int>(splines_.size()) - 1;
        }
        void registerFunction(const std::string& id, int node) { functions_[id] = node; }
        int function(const std::string& id) const { return functions_.at(id); }
        bool hasFunction(const std::string& id) const { return functions_.count(id) != 0; }

        NormalNoise& noise(int index) { return noises_[static_cast<std::size_t>(index)]; }
        const NormalNoise& noise(int index) const { return noises_[static_cast<std::size_t>(index)]; }
        BlendedNoise& blendedNoise(int index) { return blended_[static_cast<std::size_t>(index)]; }
        const std::vector<CubicSpline>& splines() const { return splines_; }
        static SampleCache& sampleCache() { static thread_local SampleCache c; return c; }

        // Bind the thread's cache to this graph, dropping any state left by a
        // previous graph. Cheap: a single pointer compare in the steady state.
        SampleCache& claimCache() const {
            SampleCache& c = sampleCache();
            if (c.owner != this) {
                c = SampleCache{};
                c.owner = this;
            }
            return c;
        }
        const std::vector<Node>& nodes() const { return nodes_; }
        std::size_t functionCount() const { return functions_.size(); }
        std::size_t nodeCount() const { return nodes_.size(); }
        bool isPure2D(int index) const {
            const std::size_t i = static_cast<std::size_t>(index);
            return i < pure2d_.size() && pure2d_[i] != 0;
        }
        std::size_t pure2DCount() const { std::size_t n = 0; for (auto v : pure2d_) n += v; return n; }

        // ---- evaluation ---------------------------------------------------

        // NoiseFunction's samplers compute
        //     shifted:    blockX * xzScale + shiftX
        //     unshifted:  blockX * xzScale
        // in DOUBLE arithmetic: the block coordinate is scaled first and the shift
        // (a float, widened exactly) is added afterwards. There is no float
        // rounding of the coordinate+shift sum, so `x*0.25 + s` is the whole story.
        static double shifted(int coordinate, double shift, double scale) {
            return static_cast<double>(coordinate) * scale + shift;
        }

        float eval(int index, int x, int y, int z) const {
            SampleCache& c = claimCache();
            const std::size_t i = static_cast<std::size_t>(index);
            const bool p2 = isPure2D(static_cast<int>(i));

            if (p2) {
                if (c.epoch2d == 0 || c.x2 != x || c.z2 != z) {
                    if (c.stamps2d.size() != nodes_.size()) {
                        c.stamps2d.assign(nodes_.size(), 0);
                        c.values2d.resize(nodes_.size());
                    }
                    if (++c.epoch2d == 0) {
                        std::fill(c.stamps2d.begin(), c.stamps2d.end(), 0u);
                        c.epoch2d = 1;
                    }
                    c.x2 = x;
                    c.z2 = z;
                }
                if (c.stamps2d[i] == c.epoch2d) return c.values2d[i];
                const float r = evalUncached(index, x, y, z);
                c.stamps2d[i] = c.epoch2d;
                c.values2d[i] = r;
                return r;
            }

            if (c.epoch3d == 0 || c.x3 != x || c.y3 != y || c.z3 != z) {
                if (c.stamps3d.size() != nodes_.size()) {
                    c.stamps3d.assign(nodes_.size(), 0);
                    c.values3d.resize(nodes_.size());
                }
                if (++c.epoch3d == 0) {
                    std::fill(c.stamps3d.begin(), c.stamps3d.end(), 0u);
                    c.epoch3d = 1;
                }
                c.x3 = x;
                c.y3 = y;
                c.z3 = z;
            }
            if (c.stamps3d[i] == c.epoch3d) return c.values3d[i];
            const float r = evalUncached(index, x, y, z);
            c.stamps3d[i] = c.epoch3d;
            c.values3d[i] = r;
            return r;
        }

        // Marks every node whose value is independent of y. Pure 2D nodes are
        // memoised per column instead of per sample point, which is what turns
        // the spline/climate tree from O(384) work per column into O(1).
        // Call once after the graph is fully built.
        void computePurity() {
            const std::size_t count = nodes_.size();
            pure2d_.assign(count, 0);
            bool changed = true;
            while (changed) {
                changed = false;
                for (std::size_t i = 0; i < count; ++i) {
                    if (pure2d_[i] != 0) continue;
                    const Node& n = nodes_[i];
                    bool pure = false;
                    switch (n.kind) {
                        case Kind::Constant:
                            pure = true;
                            break;
                        case Kind::Noise2d:
                            pure = (n.a < 0 || pure2d_[static_cast<std::size_t>(n.a)] != 0) &&
                                   (n.b < 0 || pure2d_[static_cast<std::size_t>(n.b)] != 0);
                            break;
                        case Kind::ShiftA:
                        case Kind::ShiftB:
                            pure = true;
                            break;
                        case Kind::Add: case Kind::Sub: case Kind::Mul: case Kind::Div:
                        case Kind::Min: case Kind::Max: case Kind::Abs:
                        case Kind::HalfNegative: case Kind::QuarterNegative: case Kind::Clamp:
                        case Kind::LerpConst: case Kind::LerpConstFirst:
                            pure = (n.a < 0 || pure2d_[static_cast<std::size_t>(n.a)] != 0) &&
                                   (n.b < 0 || pure2d_[static_cast<std::size_t>(n.b)] != 0);
                            break;
                        case Kind::Lerp3: // three children: alpha, first, second
                            pure = (n.a < 0 || pure2d_[static_cast<std::size_t>(n.a)] != 0) &&
                                   (n.b < 0 || pure2d_[static_cast<std::size_t>(n.b)] != 0) &&
                                   (n.c < 0 || pure2d_[static_cast<std::size_t>(n.c)] != 0);
                            break;
                        case Kind::Spline:
                            // A spline's inputs are not in the node's a/b fields at
                            // all: its coordinate and every breakpoint live inside the
                            // (possibly nested) CubicSpline, so the whole subtree has
                            // to be walked. Missing this is silent -- the node is then
                            // treated as y-independent and returns a stale value for
                            // every y in the column.
                            pure = splineIsPure2D(splines()[static_cast<std::size_t>(n.spline)]);
                            break;
                        default:
                            // Noise3d, Blended and Interpolated read y explicitly.
                            // YLinearGradient does too, and is the easy one to miss:
                            // it looks like a pure constant pair from the outside but
                            // its value is a ramp along y. Treating it as y-independent
                            // silently freezes every node above it to a single value
                            // for the whole column.
                            pure = false;
                            break;
                    }
                    if (pure) {
                        pure2d_[i] = 1;
                        changed = true;
                    }
                }
            }
        }

        float evalUncached(int index, int x, int y, int z) const {
            const Node& n = nodes_[static_cast<std::size_t>(index)];
            switch (n.kind) {
                case Kind::Constant:
                    return n.c0;

                case Kind::Noise2d: {
                    // shiftedNoise2d feeds the shift noises into x and z and samples
                    // with a y scale of zero, so the y argument is literally 0.
                    const double sx = n.a >= 0 ? eval(n.a, x, y, z) : 0.0;
                    const double sz = n.b >= 0 ? eval(n.b, x, y, z) : 0.0;
                    return noise(n.noise).getValue(shifted(x, sx, n.d0), 0.0, shifted(z, sz, n.d0));
                }

                case Kind::Noise3d: {
                    const double sx = n.a >= 0 ? eval(n.a, x, y, z) : 0.0;
                    const double sz = n.b >= 0 ? eval(n.b, x, y, z) : 0.0;
                    return noise(n.noise).getValue(shifted(x, sx, n.d0), y * n.d1, shifted(z, sz, n.d0));
                }

                case Kind::ShiftA:
                    // shiftA samples at (x * 0.25, 0, z * 0.25) and scales by 4.
                    return noise(n.noise).getValue(x * 0.25, 0.0, z * 0.25) * 4.0f;

                case Kind::ShiftB:
                    // shiftB swaps x and z; this asymmetry is intentional in vanilla.
                    return noise(n.noise).getValue(z * 0.25, 0.0, x * 0.25) * 4.0f;

                case Kind::Blended:
                    return blended_[static_cast<std::size_t>(n.noise)].sample(x, y, z);

                case Kind::Add:
                    return eval(n.a, x, y, z) + eval(n.b, x, y, z);

                case Kind::Sub:
                    return eval(n.a, x, y, z) - eval(n.b, x, y, z);

                case Kind::Mul:
                    return eval(n.a, x, y, z) * eval(n.b, x, y, z);

                case Kind::Div:
                    return eval(n.a, x, y, z) / eval(n.b, x, y, z);

                case Kind::Min: {
                    const float a = eval(n.a, x, y, z);
                    const float b = eval(n.b, x, y, z);
                    return a < b ? a : b;
                }

                case Kind::Max: {
                    const float a = eval(n.a, x, y, z);
                    const float b = eval(n.b, x, y, z);
                    return a > b ? a : b;
                }

                case Kind::Abs:
                    return std::fabs(eval(n.a, x, y, z));

                case Kind::Clamp: {
                    const float v = eval(n.a, x, y, z);
                    return v < n.c0 ? n.c0 : (v > n.c1 ? n.c1 : v);
                }

                // DensityFunctions.halfNegative(x)
                case Kind::HalfNegative:
                    return leakyReLU(eval(n.a, x, y, z), 0.5f);

                // DensityFunctions.quarterNegative(x)
                case Kind::QuarterNegative:
                    return leakyReLU(eval(n.a, x, y, z), 0.25f);

                case Kind::Lerp3: {
                    const float alpha = eval(n.a, x, y, z);
                    if (alpha == 0.0f) return eval(n.b, x, y, z);
                    if (alpha == 1.0f) return eval(n.c, x, y, z);
                    const float first = eval(n.b, x, y, z);
                    const float second = eval(n.c, x, y, z);
                    return first + alpha * (second - first);
                }

                case Kind::LerpConstFirst: {
                    const float alpha = eval(n.a, x, y, z);
                    if (alpha == 0.0f) return n.c0;
                    if (alpha == 1.0f) return eval(n.b, x, y, z);
                    const float second = eval(n.b, x, y, z);
                    return n.c0 + alpha * (second - n.c0);
                }

                case Kind::LerpConst: {
                    const float alpha = eval(n.a, x, y, z);
                    if (alpha == 0.0f) return eval(n.b, x, y, z);
                    if (alpha == 1.0f) return n.c0;
                    const float first = eval(n.b, x, y, z);
                    return first + alpha * (n.c0 - first);
                }

                case Kind::YLinearGradient: {
                    const int from = n.lowerBound;
                    const int clamped = y < from ? from : (y > n.toY ? n.toY : y);
                    return n.c0 + static_cast<float>(clamped - from) * n.c2;
                }

                case Kind::Interpolated:
                    return evalInterpolated(index, n, x, y, z);

                case Kind::Spline:
                    return applySpline(static_cast<std::size_t>(n.spline), x, y, z);

                case Kind::FindTopSurface:
                    return static_cast<float>(evalFindTopSurface(n, x, z));
            }
            return 0.0f;
        }

    private:
        bool splineIsPure2D(const CubicSpline& sp) const {
            if (sp.coordinate() >= 0 && !isPure2D(sp.coordinate())) return false;
            for (const auto& point : sp.points()) {
                if (point.child >= 0 && !splineIsPure2D(sp.child(point.child))) return false;
            }
            return true;
        }

        int push(Node n) {
            nodes_.push_back(n);
            return static_cast<int>(nodes_.size()) - 1;
        }

        int binary(Kind kind, int a, int b) {
            Node n;
            n.kind = kind;
            n.a = a;
            n.b = b;
            return push(n);
        }

        int unary(Kind kind, int a) {
            Node n;
            n.kind = kind;
            n.a = a;
            return push(n);
        }

        const CubicSpline& spline(std::size_t index) const { return splines_[index]; }

        // Samples the input at the eight cell corners surrounding the sample point and
        // trilinearly interpolates. The vanilla sampler works on volumes; the value
        // formula is a straight Mth.lerp2/lerp3 with alpha = offset / cellSize.
        float evalInterpolated(int index, const Node& n, int x, int y, int z) const {
            const int cellXz = n.cellSizeXz;
            const int cellY = n.cellSizeY;
            const int minCellX = floorDiv(x, cellXz);
            const int minCellY = floorDiv(y, cellY);
            const int minCellZ = floorDiv(z, cellXz);

            SampleCache& c = claimCache();
            if (c.cells.size() != nodes_.size()) c.cells.resize(nodes_.size());
            Cell& cell = c.cells[static_cast<std::size_t>(index)];

            if (!cell.valid || cell.x != minCellX || cell.y != minCellY || cell.z != minCellZ) {
                const int x0 = minCellX * cellXz;
                const int y0 = minCellY * cellY;
                const int z0 = minCellZ * cellXz;
                cell.v[0] = eval(n.a, x0,           y0,           z0);
                cell.v[1] = eval(n.a, x0 + cellXz,  y0,           z0);
                cell.v[2] = eval(n.a, x0,           y0 + cellY,   z0);
                cell.v[3] = eval(n.a, x0 + cellXz,  y0 + cellY,   z0);
                cell.v[4] = eval(n.a, x0,           y0,           z0 + cellXz);
                cell.v[5] = eval(n.a, x0 + cellXz,  y0,           z0 + cellXz);
                cell.v[6] = eval(n.a, x0,           y0 + cellY,   z0 + cellXz);
                cell.v[7] = eval(n.a, x0 + cellXz,  y0 + cellY,   z0 + cellXz);
                cell.x = minCellX;
                cell.y = minCellY;
                cell.z = minCellZ;
                cell.valid = true;
            }

            // InterpolatedFunction precomputes 1.0F / cellSize and multiplies.
            const float invXz = 1.0f / static_cast<float>(cellXz);
            const float invY = 1.0f / static_cast<float>(cellY);
            const float alphaX = static_cast<float>(x - minCellX * cellXz) * invXz;
            const float alphaY = static_cast<float>(y - minCellY * cellY) * invY;
            const float alphaZ = static_cast<float>(z - minCellZ * cellXz) * invXz;

            return lerp(alphaZ, lerp2(alphaX, alphaY, cell.v[0], cell.v[1], cell.v[2], cell.v[3]),
                        lerp2(alphaX, alphaY, cell.v[4], cell.v[5], cell.v[6], cell.v[7]));
        }

        // Scans downward from the rounded-down upper bound in cellHeight steps for the
        // first Y where the density function is positive.
        int evalFindTopSurface(const Node& n, int x, int z) const {
            const float upperBound = eval(n.b, x, 0, z);
            const int cellHeight = n.cellSizeY;
            int topY = static_cast<int>(std::floor(upperBound / static_cast<float>(cellHeight))) * cellHeight;
            if (topY <= n.lowerBound) return n.lowerBound;
            for (int probeY = topY; probeY >= n.lowerBound; probeY -= cellHeight) {
                if (eval(n.a, x, probeY, z) > 0.0f) return probeY;
            }
            return n.lowerBound;
        }

        float applySpline(std::size_t index, int x, int y, int z) const {
            return spline(index).apply([&](int fn) { return eval(fn, x, y, z); });
        }

        static int floorDiv(int a, int b) {
            const int q = a / b;
            return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
        }
        static float leakyReLU(float v, float negativeFactor) { return v > 0.0f ? v : v * negativeFactor; }
        static float lerp(float alpha, float p0, float p1) { return p0 + alpha * (p1 - p0); }
        static float lerp2(float a1, float a2, float x00, float x10, float x01, float x11) {
            return lerp(a2, lerp(a1, x00, x10), lerp(a1, x01, x11));
        }

        std::vector<Node> nodes_;
        std::vector<std::uint8_t> pure2d_;
        std::vector<NormalNoise> noises_;
        std::vector<BlendedNoise> blended_;
        std::vector<CubicSpline> splines_;
        std::unordered_map<std::string, int> functions_;
    };

}
