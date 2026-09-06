#pragma once
#include "Templates/ChunkTemplate.hpp"
#include "Templates/ModelTemplate.hpp"
#include "Templates/RegionTemplate.hpp"
#include "Templates/TextureTemplate.hpp"

namespace kc {
    class IO;

    // Main Processor Which Holds all Templates
    class IOBuiltInTemplates {
    public:
        explicit IOBuiltInTemplates(IO& io) : chunkTemplate(io), regionTemplate(io, chunkTemplate),
                                        textureTemplate(io), modelTemplate(io, textureTemplate) {}

        ChunkTemplate& getChunkTemplate() { return chunkTemplate; }
        RegionTemplate& getRegionTemplate() { return regionTemplate; }

        ModelTemplate& getModelTemplate() { return modelTemplate; }
        TextureTemplate& getTextureTemplate() { return textureTemplate; }

    private:
        ChunkTemplate chunkTemplate;
        RegionTemplate regionTemplate;

        TextureTemplate textureTemplate;
        ModelTemplate modelTemplate;

    };
}
