#pragma once
#include "Templates/ChunkTemplate.hpp"
#include "Templates/RegionTemplate.hpp"

namespace lve {
    class IO;

    // Main Processor Which Holds all Templates
    class IOBuiltInTemplates {
    public:
        explicit IOBuiltInTemplates(IO& io) : chunkTemplate(io), regionTemplate(io, chunkTemplate) {}

        ChunkTemplate& getChunkTemplate() { return chunkTemplate; }
        RegionTemplate& getRegionTemplate() { return regionTemplate; }

    private:
        ChunkTemplate chunkTemplate;
        RegionTemplate regionTemplate;

    };
}