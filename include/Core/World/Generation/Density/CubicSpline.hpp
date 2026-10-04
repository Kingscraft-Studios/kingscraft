#pragma once

#include <cstddef>
#include <vector>

namespace kc {

    // TerrainProvider builds its splines through a Float2FloatFunction "value
    // transformer". The important subtlety is that vanilla applies the transformer
    // to each constant point VALUE at addPoint() time, not to the spline input:
    //
    //   addPoint(loc, value) -> addPoint(loc, new Constant(valueTransformer.apply(value)), 0)
    //
    // So a nested spline built with the same transformer has already had it folded
    // into its own leaves. Evaluating the input instead would change every
    // amplified preset, and would double-apply here.
    enum class SplineValueTransform {
        Identity,
        AmplifiedOffset,      // offset < 0 ? offset : offset * 2
        AmplifiedFactor,      // 1.25 - 6.25 / (factor + 5)
        AmplifiedJaggedness,  // jaggedness * 2
    };

    inline float transformSplineValue(SplineValueTransform transform, float value) {
        switch (transform) {
            case SplineValueTransform::AmplifiedOffset:
                return value < 0.0f ? value : value * 2.0f;
            case SplineValueTransform::AmplifiedFactor:
                return 1.25f - 6.25f / (value + 5.0f);
            case SplineValueTransform::AmplifiedJaggedness:
                return value * 2.0f;
            case SplineValueTransform::Identity:
            default:
                return value;
        }
    }

    // Port of CubicSpline (net.minecraft.util.CubicSpline) with the coordinate
    // replaced by a density-function node index and the nested values by indices
    // into a child table.
    //
    // Vanilla is a sealed interface of Multipoint (a piecewise cubic Hermite over
    // ascending locations) and Constant (a leaf). Between two points the value is
    //
    //   lerp(t, y1, y2) + t * (1 - t) * lerp(t, a, b)
    //   a = d1 * (x2 - x1) - (y2 - y1)
    //   b = -d2 * (x2 - x1) + (y2 - y1)
    //
    // and outside the range it linearly extends using the nearest derivative.
    class CubicSpline {
    public:
        struct Point {
            float location = 0.0f;
            float derivative = 0.0f;
            float constant = 0.0f;
            int child = -1;  // >= 0 selects children_[child], otherwise `constant`
        };

        int coordinate() const { return coordinate_; }
        SplineValueTransform transform() const { return transform_; }
        const std::vector<Point>& points() const { return points_; }
        const std::vector<CubicSpline>& children() const { return children_; }
        const CubicSpline& child(int index) const { return children_[static_cast<std::size_t>(index)]; }

        // `coordinateFn` maps a density-function node index to its value at the
        // current sample. Nested splines have their own coordinates, so the same
        // callback is reused all the way down the tree.
        template <class CoordinateFn>
        float apply(const CoordinateFn& coordinateFn) const {
            const float input = coordinateFn(coordinate_);
            const int start = findIntervalStart(input);
            const int last = static_cast<int>(points_.size()) - 1;

            if (start < 0) {
                return linearExtend(input, samplePoint(points_[0], coordinateFn), points_[0]);
            }
            if (start >= last) {
                return linearExtend(input, samplePoint(points_[static_cast<std::size_t>(last)], coordinateFn),
                                    points_[static_cast<std::size_t>(last)]);
            }

            const Point& p0 = points_[static_cast<std::size_t>(start)];
            const Point& p1 = points_[static_cast<std::size_t>(start) + 1];
            const float x1 = p0.location;
            const float x2 = p1.location;
            const float t = (input - x1) / (x2 - x1);
            const float y1 = samplePoint(p0, coordinateFn);
            const float y2 = samplePoint(p1, coordinateFn);
            const float a = p0.derivative * (x2 - x1) - (y2 - y1);
            const float b = -p1.derivative * (x2 - x1) + (y2 - y1);
            return lerp(t, y1, y2) + t * (1.0f - t) * lerp(t, a, b);
        }

    private:
        template <class CoordinateFn>
        float samplePoint(const Point& point, const CoordinateFn& coordinateFn) const {
            return point.child >= 0 ? children_[static_cast<std::size_t>(point.child)].apply(coordinateFn)
                                    : point.constant;
        }

        static float linearExtend(float input, float value, const Point& point) {
            return point.derivative == 0.0f ? value : value + point.derivative * (input - point.location);
        }

        // Mth.binarySearch(0, n, i -> input < locations[i]) - 1, i.e. lower_bound - 1.
        // Locations are strictly ascending (vanilla rejects anything else), so the
        // left half of the binary search is a valid lower_bound.
        int findIntervalStart(float input) const {
            int i = 0;
            int j = static_cast<int>(points_.size());
            while (i < j) {
                const int mid = i + (j - i) / 2;
                if (input < points_[static_cast<std::size_t>(mid)].location) {
                    j = mid;
                } else {
                    i = mid + 1;
                }
            }
            return i - 1;
        }

        // Alpha first, then the endpoints: this matches vanilla's
        // MathHelper.lerp(delta, start, end). It does NOT match
        // std::lerp(start, end, alpha), and the two orders are easy to
        // transpose by eye when transcribing vanilla's call sites.
        static float lerp(float alpha, float p0, float p1) { return p0 + alpha * (p1 - p0); }

        int coordinate_ = -1;
        SplineValueTransform transform_ = SplineValueTransform::Identity;
        std::vector<Point> points_;
        std::vector<CubicSpline> children_;

        friend class CubicSplineBuilder;
    };

    // Mirrors CubicSpline.Builder, including the ascending-order assertion and the
    // fact that the value transformer is baked into constant points as they are
    // added.
    class CubicSplineBuilder {
    public:
        CubicSplineBuilder(int coordinate, SplineValueTransform transform)
            : transform_(transform) {
            spline_.coordinate_ = coordinate;
            spline_.transform_ = transform;
        }

        CubicSplineBuilder& addPoint(float location, float value, float derivative = 0.0f) {
            CubicSpline::Point point;
            point.location = location;
            point.derivative = derivative;
            point.constant = transformSplineValue(transform_, value);
            point.child = -1;
            push(std::move(point));
            return *this;
        }

        CubicSplineBuilder& addNestedPoint(float location, const CubicSpline& nested, float derivative = 0.0f) {
            CubicSpline::Point point;
            point.location = location;
            point.derivative = derivative;
            point.child = static_cast<int>(spline_.children_.size());
            push(std::move(point));
            spline_.children_.push_back(nested);
            return *this;
        }

        CubicSpline build() { return std::move(spline_); }

    private:
        void push(CubicSpline::Point point) {
            if (!spline_.points_.empty() && point.location <= spline_.points_.back().location) {
                throw std::invalid_argument("Please register points in ascending order");
            }
            spline_.points_.push_back(std::move(point));
        }

        SplineValueTransform transform_;
        CubicSpline spline_;
    };

}