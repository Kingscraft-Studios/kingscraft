#pragma once

namespace lve {

template<typename T> class Registry;

template<typename T>
class RegistryKey {
    int id_ = -1;
    friend class Registry<T>;
public:
    RegistryKey() = default;

        int getId() const { return id_; }
        explicit operator bool() const { return id_ >= 0; }

    T* operator->() const;
    operator T*() const;
    operator T&() const;

    bool operator==(const RegistryKey& o) const { return id_ == o.id_; }
    bool operator!=(const RegistryKey& o) const { return id_ != o.id_; }
    bool operator<(const RegistryKey& o) const { return id_ < o.id_; }
};

} // namespace lve
