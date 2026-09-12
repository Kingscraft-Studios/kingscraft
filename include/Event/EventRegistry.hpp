#pragma once
#include <mutex>
#include <unordered_set>

#include "Events/RegistryReloadEvent.hpp"
#include "Events/RegistryReloadPostEvent.hpp"
#include "Events/RegistryReloadPreEvent.hpp"
#include "detail/TypeId.hpp"
#include "detail/TypeTag.hpp"

namespace kc {

    namespace detail {

        template<typename... Ts>
        struct TypeList {};

        template<typename List>
        struct TypeListFor;

        template<typename... Ts>
        struct TypeListFor<TypeList<Ts...>> {
            template<typename Fn>
            static void forEach(Fn&& fn) {
                (fn(TypeTag<Ts>{}), ...);
            }
        };

    }

    // TODO
    // COMPILE-TIME list of every event type the engine can dispatch.
    // When a new event class is created, add it here (one line). This is the
    // list registerListener<L>() folds over to discover L's handlers.
    using EventRegistryTypes = detail::TypeList<
        RegistryReloadPreEvent,
        RegistryReloadEvent,
        RegistryReloadPostEvent
    >;

    // Runtime, auto-populated "which types have been seen/fired" directory.
    class EventRegistry {
    public:
        static EventRegistry& get();

        template<typename EventType>
        void ensure() {
            ensureId(detail::typeId<EventType>());
        }

        bool contains(uint64_t typeId) const {
            std::lock_guard<std::mutex> lock(mutex);
            return known.count(typeId) > 0;
        }

        size_t size() const {
            std::lock_guard<std::mutex> lock(mutex);
            return known.size();
        }

    private:
        EventRegistry() = default;
        void ensureId(uint64_t typeId);

        mutable std::mutex mutex;
        std::unordered_set<uint64_t> known;

    };


}
