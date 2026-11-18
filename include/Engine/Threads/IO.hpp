#pragma once
#include <memory>

#include "IO/DiskLayer.hpp"
#include "IO/IOBuiltInTemplates.hpp"


namespace kc {
    // The IO thread's owner. It exposes nothing but the built-in templates:
    // every subsystem asks a template for the data it needs and never sees a
    // path, a stream or a serialization format.
    class IO {
    public:

        static void Init();
        static void Shutdown();
        static IO& Get();

        IOBuiltInTemplates& getBuiltinTemplates() { return builtInTemplates; }

    private:
        DiskLayer disk_;
        IOBuiltInTemplates builtInTemplates{disk_};
        static std::unique_ptr<IO> instance_;
    };
} // namespace kc
