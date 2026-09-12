#pragma once

#include "Event/EventPriority.hpp"
#include "Event/Events/Event.hpp"
#include "Event/Listener.hpp"
#include "Event/detail/TypeTag.hpp"

// --- preprocessor helpers --------------------------------------------------
#define KC_DETAIL_CAT_(a, b) a##b
#define KC_DETAIL_CAT(a, b) KC_DETAIL_CAT_(a, b)

#define KC_DETAIL_RSEQ_N() 3, 2, 1, 0
#define KC_DETAIL_ARG_N(_1, _2, _3, N, ...) N
#define KC_DETAIL_NARG_(...) KC_DETAIL_ARG_N(__VA_ARGS__)
#define KC_DETAIL_NARG(...) KC_DETAIL_NARG_(__VA_ARGS__, KC_DETAIL_RSEQ_N())

// --- handler annotation -----------------------------------------------------
//   KC_EVENT_HANDLER(EventType, method)                       -> Normal
//   KC_EVENT_HANDLER(EventType, method, EventPriority::High)  -> custom
//   KC_EVENT_HANDLER(EventType)                               -> method = on<EventType>
//                                                               (unqualified type name only)
#define KC_DETAIL_EVENT_HANDLER(EventType, MethodName, Priority)          \
    friend struct ::kc::detail::EventRegistrar;                            \
    template<typename Self>                                               \
    static void kcOnEvent(::kc::Listener& self, ::kc::Event& event,       \
                          ::kc::detail::TypeTag<EventType>* tag) {        \
        static_cast<Self&>(self).MethodName(static_cast<EventType&>(event)); \
    }                                                                     \
    static constexpr ::kc::EventPriority kcPriority(                      \
        ::kc::detail::TypeTag<EventType>* tag) {                          \
        return Priority;                                                  \
    }

#define KC_EVENT_HANDLER(...) \
    KC_DETAIL_CAT(KC_EVENT_HANDLER_, KC_DETAIL_NARG(__VA_ARGS__))(__VA_ARGS__)

#define KC_EVENT_HANDLER_1(EventType) \
    KC_DETAIL_EVENT_HANDLER(EventType, KC_DETAIL_CAT(on, EventType), ::kc::EventPriority::Normal)
#define KC_EVENT_HANDLER_2(EventType, MethodName) \
    KC_DETAIL_EVENT_HANDLER(EventType, MethodName, ::kc::EventPriority::Normal)
#define KC_EVENT_HANDLER_3(EventType, MethodName, Priority) KC_DETAIL_EVENT_HANDLER(EventType, MethodName, Priority)