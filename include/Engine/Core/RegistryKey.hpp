#pragma once

#include <cstdint>
#include <string>
#include <utility>

namespace kc {

template<typename T> class Registry;

template<typename T>
class RegistryKey {
    uint64_t encoded_ = 0;
    std::string identifier_;
    friend class Registry<T>;
public:
    RegistryKey() = default;
    RegistryKey(uint64_t encoded, std::string identifier)
        : encoded_(encoded), identifier_(std::move(identifier)) {}

    uint64_t getEncoded() const { return encoded_; }
    const std::string& getIdentifier() const { return identifier_; }

    explicit operator bool() const { return encoded_ != 0; }

    T* operator->() const;
    operator T*() const;
    operator T&() const;

    bool operator==(const RegistryKey& o) const { return encoded_ == o.encoded_; }
    bool operator!=(const RegistryKey& o) const { return encoded_ != o.encoded_; }
    bool operator<(const RegistryKey& o) const { return encoded_ < o.encoded_; }
};

} // namespace kc