#pragma once

#include "Bus/Message.hpp"
#include "Event/EventPriority.hpp"
#include "Event/Events/Event.hpp"
#include "Event/Listener.hpp"
#include "Event/detail/TypeTag.hpp"

// --- preprocessor helpers --------------------------------------------------
#define KC_DETAIL_CAT_(a, b) a##b
#define KC_DETAIL_CAT(a, b) KC_DETAIL_CAT_(a, b)

#define KC_DETAIL_RSEQ_N() 4, 3, 2, 1, 0
#define KC_DETAIL_ARG_N(_1, _2, _3, _4, N, ...) N
#define KC_DETAIL_NARG_(...) KC_DETAIL_ARG_N(__VA_ARGS__)
#define KC_DETAIL_NARG(...) KC_DETAIL_NARG_(__VA_ARGS__, KC_DETAIL_RSEQ_N())

// --- handler annotation -----------------------------------------------------
//   KC_EVENT_HANDLER(EventType, ThreadName)                                -> on<EventType>, Normal
//   KC_EVENT_HANDLER(EventType, method, ThreadName)                        -> Normal
//   KC_EVENT_HANDLER(EventType, method, priority, ThreadName)              -> custom priority
//   KC_EVENT_HANDLER(EventType)                                            -> on<EventType>, Normal, GameLogic
#define KC_DETAIL_EVENT_HANDLER(EventType, MethodName, Priority, Thread)    \
    friend struct ::kc::detail::EventRegistrar;                              \
    template<typename Self>                                                 \
    static void kcOnEvent(::kc::Listener& self, ::kc::Event& event,         \
                          ::kc::detail::TypeTag<EventType>* tag) {          \
        static_cast<Self&>(self).MethodName(static_cast<EventType&>(event)); \
    }                                                                       \
    static constexpr ::kc::EventPriority kcPriority(                        \
        ::kc::detail::TypeTag<EventType>* tag) {                            \
        return Priority;                                                    \
    }                                                                       \
    static constexpr ::kc::ThreadName kcThread(                             \
        ::kc::detail::TypeTag<EventType>* tag) {                            \
        return Thread;                                                      \
    }

#define KC_EVENT_HANDLER(...) \
    KC_DETAIL_CAT(KC_EVENT_HANDLER_, KC_DETAIL_NARG(__VA_ARGS__))(__VA_ARGS__)

#define KC_EVENT_HANDLER_1(EventType) \
    KC_DETAIL_EVENT_HANDLER(EventType, KC_DETAIL_CAT(on, EventType), ::kc::EventPriority::Normal, ::kc::ThreadName::GameLogic)
#define KC_EVENT_HANDLER_2(EventType, ThreadArg) \
    KC_DETAIL_EVENT_HANDLER(EventType, KC_DETAIL_CAT(on, EventType), ::kc::EventPriority::Normal, ThreadArg)
#define KC_EVENT_HANDLER_3(EventType, MethodName, ThreadArg) \
    KC_DETAIL_EVENT_HANDLER(EventType, MethodName, ::kc::EventPriority::Normal, ThreadArg)
#define KC_EVENT_HANDLER_4(EventType, MethodName, Priority, ThreadArg) \
    KC_DETAIL_EVENT_HANDLER(EventType, MethodName, Priority, ThreadArg)
