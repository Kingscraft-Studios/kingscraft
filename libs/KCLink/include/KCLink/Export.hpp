#pragma once

#if defined(_WIN32) && defined(KCLINK_BUILD)
#define KCLINK_API __declspec(dllexport)
#elif defined(_WIN32)
#define KCLINK_API __declspec(dllimport)
#elif defined(__GNUC__) || defined(__clang__)
#define KCLINK_API __attribute__((visibility("default")))
#else
#define KCLINK_API
#endif
