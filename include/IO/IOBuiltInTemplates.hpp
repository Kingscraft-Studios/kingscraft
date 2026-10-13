#pragma once
#include "Templates/ChunkTemplate.hpp"
#include "Templates/GenericCacheTemplate.hpp"
#include "Templates/ModelTemplate.hpp"
#include "Templates/RegionTemplate.hpp"
#include "Templates/TextureTemplate.hpp"
#include "Templates/WorldMetadataTemplate.hpp"

namespace kc {
    class IO;

    // Main Processor Which Holds all Templates
    class IOBuiltInTemplates {
    public:
        explicit IOBuiltInTemplates(IO& io) : chunkTemplate(io), regionTemplate(io, chunkTemplate),
                                        worldMetadataTemplate(io), textureTemplate(io),
                                        modelTemplate(io, textureTemplate), cacheTemplate(io) {}

        ChunkTemplate& getChunkTemplate() { return chunkTemplate; }
        RegionTemplate& getRegionTemplate() { return regionTemplate; }

        WorldMetadataTemplate& getWorldMetadataTemplate() { return worldMetadataTemplate; }

        ModelTemplate& getModelTemplate() { return modelTemplate; }
        TextureTemplate& getTextureTemplate() { return textureTemplate; }

        GenericCacheTemplate& getCacheTemplate() { return cacheTemplate; }

    private:
        ChunkTemplate chunkTemplate;
        RegionTemplate regionTemplate;
        WorldMetadataTemplate worldMetadataTemplate;

        TextureTemplate textureTemplate;
        ModelTemplate modelTemplate;

        GenericCacheTemplate cacheTemplate;
    };
}
