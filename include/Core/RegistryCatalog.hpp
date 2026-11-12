#pragma once

#include <string>
#include <utility>
#include <vector>

#include "Core/Registry.hpp"

namespace kc {

using RegistryBuildFn = void (*)(int&);

class RegistryCatalog {
public:
    struct Entry {
        std::string name;
        IRegistryBase* registry;
        RegistryBuildFn build;
        int order;
    };

    // Registers a registry type and the function that populates it. Place one
    // instance at namespace scope in the registration class's .cpp so adding a
    // new registry type does not require editing any central file.
    template<typename T>
    struct Registration {
        Registration(std::string name, RegistryBuildFn build, int order = 0) {
            RegistryCatalog::add<T>(std::move(name), build, order);
        }
    };

    template<typename T>
    static void add(std::string name, RegistryBuildFn build, int order = 0) {
        insert(Entry{std::move(name), &Registry<T>::getRegistry(), build, order});
    }

    // Calls every registered builder in ascending order (stable for equal
    // orders), decrementing pending through the builders.
    static void buildAll(int& pending);

    // Clears every registered registry through the type-erased base.
    static void clearAll();

    static const std::vector<Entry>& entries();

private:
    static void insert(Entry entry);
};

} // namespace kc
