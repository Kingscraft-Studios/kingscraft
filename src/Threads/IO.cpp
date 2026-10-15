#include "Threads/IO.hpp"

namespace kc {

    std::unique_ptr<IO> IO::instance_ = nullptr;

    void IO::Init() {
        instance_ = std::make_unique<IO>();
    }

    void IO::Shutdown() {
        if (!instance_) return;
        if (!instance_->builtInTemplates.flush())
            LogUtils::error(ThreadName::Engine, "IO shutdown flush failed");
        instance_.reset();
    }

    IO& IO::Get() {
        return *instance_;
    }
} // namespace kc
