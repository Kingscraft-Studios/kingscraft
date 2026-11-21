#pragma once

#ifndef KCLINK_INTERNAL_COMPILATION
#error "KCLink internal header: not part of the public API"
#endif

namespace kc::link::detail {

    // Move-only RAII wrapper around an OS descriptor. All member functions
    // are defined in the library's private sources and are not exported.
    class UniqueFd {
    public:
        UniqueFd() noexcept;
        explicit UniqueFd(int fd) noexcept;
        ~UniqueFd();

        UniqueFd(const UniqueFd&) = delete;
        UniqueFd& operator=(const UniqueFd&) = delete;
        UniqueFd(UniqueFd&& other) noexcept;
        UniqueFd& operator=(UniqueFd&& other) noexcept;

        int get() const noexcept;
        bool valid() const noexcept;
        int release() noexcept;
        void reset(int fd = -1) noexcept;
        void close() noexcept;

    private:
        int fd = -1;
    };

}
