#include "internal/Unix/UniqueFd.hpp"

#include <unistd.h>

namespace kc::link::detail {

    UniqueFd::UniqueFd() noexcept = default;

    UniqueFd::UniqueFd(int descriptor) noexcept : fd(descriptor) {}

    UniqueFd::~UniqueFd() { reset(); }

    UniqueFd::UniqueFd(UniqueFd&& other) noexcept : fd(other.release()) {}

    UniqueFd& UniqueFd::operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            reset();
            fd = other.release();
        }
        return *this;
    }

    int UniqueFd::get() const noexcept { return fd; }

    bool UniqueFd::valid() const noexcept { return fd >= 0; }

    int UniqueFd::release() noexcept {
        const int value = fd;
        fd = -1;
        return value;
    }

    void UniqueFd::reset(int descriptor) noexcept {
        if (fd >= 0) ::close(fd);
        fd = descriptor;
    }

    void UniqueFd::close() noexcept { reset(); }

}
