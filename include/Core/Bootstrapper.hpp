#pragma once

#include <memory>

namespace kc {

    class Bootstrapper {
    public:
        static void Init();
        static void Shutdown();
        static Bootstrapper& Get();

        void load();

    private:
        static std::unique_ptr<Bootstrapper> instance_;
    };

} // namespace kc