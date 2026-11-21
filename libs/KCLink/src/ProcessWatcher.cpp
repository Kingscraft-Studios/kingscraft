#include <KCLink/ProcessWatcher.hpp>

#include "internal/ProcessCore.hpp"

#include <memory>
#include <stdexcept>

namespace kc::link {
    namespace {

        detail::ProcessCore& requireCore(const std::unique_ptr<detail::ProcessCore>& core) {
            if (!core) throw std::logic_error("KCLink: operation on an empty process watcher");
            return *core;
        }

    }

    ProcessWatcher::ProcessWatcher(pid_t process)
        : core_(std::make_unique<detail::ProcessCore>(process)) {}

    ProcessWatcher::~ProcessWatcher() = default;

    pid_t ProcessWatcher::processId() const noexcept {
        return core_ ? core_->processId() : -1;
    }

    detail::ProcessCore& ProcessWatcher::core() noexcept { return requireCore(core_); }

    const detail::ProcessCore& ProcessWatcher::core() const noexcept { return requireCore(core_); }

}
