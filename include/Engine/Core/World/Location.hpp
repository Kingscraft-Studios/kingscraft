#pragma once

#include <cstdint>
#include <functional>
#include <glm/vec3.hpp>

namespace kc {

    // Block/Chunk location used throughout the world
    class Location {
    public:
        int x = 0;
        int y = 0;
        int z = 0;

        Location() = default;
        Location(int x_, int y_, int z_) : x(x_), y(y_), z(z_) {}
        explicit Location(const glm::ivec3& v) : x(v.x), y(v.y), z(v.z) {}
        explicit Location(const glm::vec3& v) : x(static_cast<int>(v.x)), y(static_cast<int>(v.y)), z(static_cast<int>(v.z)) {}

        glm::ivec3 toIvec3() const { return glm::ivec3(x, y, z); }
        glm::vec3 toVec3() const { return glm::vec3(x, y, z); }

        bool operator==(const Location& other) const {
            return x == other.x && y == other.y && z == other.z;
        }
        bool operator!=(const Location& other) const {
            return !(*this == other);
        }
    };

}

namespace std {
    template <>
    struct hash<kc::Location> {
        size_t operator()(const kc::Location& l) const noexcept {
            // Simple hash combination
            size_t h1 = std::hash<int>()(l.x);
            size_t h2 = std::hash<int>()(l.y);
            size_t h3 = std::hash<int>()(l.z);
            h1 ^= h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2);
            h1 ^= h3 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2);
            return h1;
        }
    };
}
