#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "Core/RegistryKey.hpp"
#include "Core/ResourceLocation.hpp"

namespace kc {

template<typename T>
class Registry {
    struct Entry {
        std::unique_ptr<T> entry;
        std::string identifier;
    };

    std::unordered_map<uint64_t, Entry> entries_;
    inline static Registry<T> instance_{};
    Registry() = default;
public:
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;

    static Registry<T>& getRegistry() { return instance_; }

    // Builds a stable handle from an identifier. Does not register the entry
    // itself; the static RegistryKey pattern relies on that (keys exist before
    // the block objects are loaded). add() performs the actual registration.
    RegistryKey<T> createKey(const ResourceLocation& location) {
        return RegistryKey<T>(location.encoded, location.identifier);
    }

    void add(const RegistryKey<T>& key, std::unique_ptr<T> entry) {
        entries_[key.encoded_] = Entry{std::move(entry), key.identifier_};
    }
    void add(const ResourceLocation& location, std::unique_ptr<T> entry) {
        entries_[location.encoded] = Entry{std::move(entry), location.identifier};
    }

    bool remove(const RegistryKey<T>& key) { return remove(key.encoded_); }
    bool remove(const ResourceLocation& location) { return remove(location.encoded); }
    bool remove(uint64_t encoded) { return entries_.erase(encoded) > 0; }

    T* get(uint64_t encoded) const {
        auto it = entries_.find(encoded);
        return (it != entries_.end()) ? it->second.entry.get() : nullptr;
    }
    T* get(const ResourceLocation& location) const { return get(location.encoded); }

    // Decode helper: the hash is one-way, but the registry keeps the raw
    // identifier, so a saved encoded value can be mapped back to its name.
    const std::string& getIdentifier(uint64_t encoded) const {
        static const std::string kEmpty;
        auto it = entries_.find(encoded);
        return (it != entries_.end()) ? it->second.identifier : kEmpty;
    }

    // Reverse lookup: encoded id of the entry holding this pointer. Returns 0
    // (the "air"/empty cell encoding) when the pointer is not registered.
    uint64_t getEncodedID(const T* entry) const {
        for (const auto& [encoded, e] : entries_)
            if (e.entry.get() == entry) return encoded;
        return 0;
    }

    size_t size() const { return entries_.size(); }

    template<typename Fn>
    void forEach(Fn&& fn) const {
        for (const auto& [encoded, e] : entries_)
            if (e.entry) fn(encoded, *e.entry);
    }
};

template<typename T>
T* RegistryKey<T>::operator->() const {
    return Registry<T>::getRegistry().get(encoded_);
}

template<typename T>
RegistryKey<T>::operator T*() const {
    return Registry<T>::getRegistry().get(encoded_);
}

template<typename T>
RegistryKey<T>::operator T&() const {
    return *Registry<T>::getRegistry().get(encoded_);
}

} // namespace kc