#pragma once

#include <memory>
#include <vector>

#include "Core/RegistryKey.hpp"

namespace lve {

template<typename T>
class Registry {
    std::vector<std::unique_ptr<T>> entries_;
    inline static Registry<T> instance_{};
    Registry() = default;
public:
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;

    static Registry<T>& getRegistry() { return instance_; }

    RegistryKey<T> createKey() {
        RegistryKey<T> key;
        key.id_ = entries_.size();
        entries_.emplace_back(nullptr);
        return key;
    }

    void add(const RegistryKey<T>& key, std::unique_ptr<T> entry) {
        if (key.id_ >= 0 && key.id_ < static_cast<int>(entries_.size())) {
            entries_[key.id_] = std::move(entry);
        }
    }

    T* get(int id) const {
        if (id < 0 || id >= static_cast<int>(entries_.size())) return nullptr;
        return entries_[id].get();
    }

    int size() const { return static_cast<int>(entries_.size()); }

    int getID(const T* entry) const {
        for (size_t i = 0; i < entries_.size(); ++i)
            if (entries_[i].get() == entry) return static_cast<int>(i);
        return -1;
    }
};

template<typename T>
T* RegistryKey<T>::operator->() const {
    return Registry<T>::getRegistry().get(id_);
}

template<typename T>
RegistryKey<T>::operator T*() const {
    return Registry<T>::getRegistry().get(id_);
}

template<typename T>
RegistryKey<T>::operator T&() const {
    return *Registry<T>::getRegistry().get(id_);
}

} // namespace lve
