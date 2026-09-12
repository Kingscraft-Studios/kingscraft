#pragma once

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Core/RegistryKey.hpp"
#include "Core/ResourceLocation.hpp"

namespace kc {

template<typename T>
class Registry {
    struct Entry {
        std::shared_ptr<T> entry;
        std::string identifier;
    };

    std::unordered_map<uint64_t, Entry> entries_;
    mutable std::shared_mutex entriesMutex_;
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
        add(key.encoded_, key.identifier_, std::move(entry));
    }
    void add(const ResourceLocation& location, std::unique_ptr<T> entry) {
        add(location.encoded, location.identifier, std::move(entry));
    }

    bool remove(const RegistryKey<T>& key) { return remove(key.encoded_); }
    bool remove(const ResourceLocation& location) { return remove(location.encoded); }
    bool remove(uint64_t encoded);

    void clear();

    T* get(uint64_t encoded) const {
        std::shared_lock<std::shared_mutex> lock(entriesMutex_);
        auto it = entries_.find(encoded);
        return (it != entries_.end()) ? it->second.entry.get() : nullptr;
    }
    T* get(const ResourceLocation& location) const { return get(location.encoded); }

    // Strong-ref lookup: callers that retain the object across an await or a
    // concurrent reload hold it alive instead of a potentially dangling raw
    // pointer. Prefer this over get() on worker threads.
    std::shared_ptr<T> getShared(uint64_t encoded) const {
        std::shared_lock<std::shared_mutex> lock(entriesMutex_);
        auto it = entries_.find(encoded);
        return (it != entries_.end()) ? it->second.entry : nullptr;
    }

    // Decode helper: the hash is one-way, but the registry keeps the raw
    // identifier, so a saved encoded value can be mapped back to its name.
    // Returned by value: the entry may be removed concurrently.
    std::string getIdentifier(uint64_t encoded) const {
        std::shared_lock<std::shared_mutex> lock(entriesMutex_);
        auto it = entries_.find(encoded);
        return (it != entries_.end()) ? it->second.identifier : std::string{};
    }

    // Reverse lookup: encoded id of the entry holding this pointer. Returns 0
    // (the "air"/empty cell encoding) when the pointer is not registered.
    uint64_t getEncodedID(const T* entry) const {
        std::shared_lock<std::shared_mutex> lock(entriesMutex_);
        for (const auto& [encoded, e] : entries_)
            if (e.entry.get() == entry) return encoded;
        return 0;
    }

    size_t size() const {
        std::shared_lock<std::shared_mutex> lock(entriesMutex_);
        return entries_.size();
    }

    template<typename Fn>
    void forEach(Fn&& fn) const {
        std::shared_lock<std::shared_mutex> lock(entriesMutex_);
        for (const auto& [encoded, e] : entries_)
            if (e.entry) fn(encoded, *e.entry);
    }

private:
    void add(uint64_t encoded, const std::string& identifier, std::unique_ptr<T> entry) {
        std::unique_lock<std::shared_mutex> lock(entriesMutex_);
        entries_[encoded] = Entry{std::shared_ptr<T>(std::move(entry)), identifier};
    }
};

template<typename T>
bool Registry<T>::remove(uint64_t encoded) {
    std::unique_lock<std::shared_mutex> lock(entriesMutex_);
    return entries_.erase(encoded) > 0;
}

template<typename T>
void Registry<T>::clear() {
    std::unique_lock<std::shared_mutex> lock(entriesMutex_);
    entries_.clear();
}

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