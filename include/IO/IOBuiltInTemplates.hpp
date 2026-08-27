#pragma once
#include "Templates/ChunkTemplate.hpp"

namespace lve {
    class IO;

    // Main Processor Which Holds all Templates
    class IOBuiltInTemplates {
    public:
        explicit IOBuiltInTemplates(IO& io) : chunkTemplate(io) {}

        ChunkTemplate& getChunkTemplate() { return chunkTemplate; }

    private:
        ChunkTemplate chunkTemplate;

    };
}
