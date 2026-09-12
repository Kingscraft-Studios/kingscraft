#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace kc {

namespace ResourceLocations {
    constexpr std::string_view DEFAULT_NAMESPACE = "kingscraft";
}

// FNV-1a 64-bit — same algorithm used by Chunk::hashBytes. Turned a string
// identifier (e.g. "kingscraft:stone") into the fast encoded uint64 format the
// registry keys on.
constexpr uint64_t hashString(std::string_view str) {
    uint64_t hash = 1469598103934665603ull;
    for (char c : str) {
        hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        hash *= 1099511628211ull;
    }
    return hash;
}

// Carries both the raw string identifier and its fast encoded (hashed) form.
// The encoding is one-way, so the registry keeps the raw string alongside the
// encoded key to be able to decode saved data back into a readable identifier.
class ResourceLocation {
public:
    std::string identifier;
    uint64_t encoded = 0;

    ResourceLocation() = default;
    ResourceLocation(std::string ns, std::string path)
        : identifier(std::move(ns) + ":" + std::move(path)) {
        encoded = hashString(identifier);
    }

    static ResourceLocation of(const std::string& ns, const std::string& path) {
        return ResourceLocation(ns, path);
    }

    static ResourceLocation withDefaultNamespace(const std::string& path) {
        return ResourceLocation(std::string(ResourceLocations::DEFAULT_NAMESPACE), path);
    }

    const std::string& getIdentifier() const { return identifier; }
    uint64_t getEncoded() const { return encoded; }

    bool operator==(const ResourceLocation& o) const { return identifier == o.identifier; }
    bool operator!=(const ResourceLocation& o) const { return identifier != o.identifier; }
};

} // namespace kc