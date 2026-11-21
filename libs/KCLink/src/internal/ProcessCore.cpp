#include "internal/ProcessCore.hpp"
#include "internal/Unix/PidFd.hpp"

namespace kc::link::detail {

    ProcessCore::ProcessCore(pid_t process) : process(process), handle_(openProcessHandle(process)) {}

    pid_t ProcessCore::processId() const noexcept { return process; }

    int ProcessCore::handle() const noexcept { return handle_.get(); }

}
