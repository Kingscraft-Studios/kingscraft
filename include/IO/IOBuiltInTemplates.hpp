#pragma once
#include "Templates/ChunkTemplate.hpp"
#include "Templates/FontTemplate.hpp"
#include "Templates/GenericCacheTemplate.hpp"
#include "Templates/LogTemplate.hpp"
#include "Templates/ModelTemplate.hpp"
#include "Templates/RegionTemplate.hpp"
#include "Templates/ShaderTemplate.hpp"
#include "Templates/TextureTemplate.hpp"
#include "Templates/WorldMetadataTemplate.hpp"

namespace kc {
    class DiskLayer;

    // Main Processor Which Holds all Templates
    class IOBuiltInTemplates {
    public:
        explicit IOBuiltInTemplates(DiskLayer& disk)
            : chunkTemplate(disk), regionTemplate(disk, chunkTemplate),
              worldMetadataTemplate(disk), textureTemplate(disk),
              modelTemplate(disk, textureTemplate), cacheTemplate(disk),
              shaderTemplate(disk), fontTemplate(disk), logTemplate(disk) {}

        ChunkTemplate& getChunkTemplate() { return chunkTemplate; }
        RegionTemplate& getRegionTemplate() { return regionTemplate; }

        WorldMetadataTemplate& getWorldMetadataTemplate() { return worldMetadataTemplate; }

        ModelTemplate& getModelTemplate() { return modelTemplate; }
        TextureTemplate& getTextureTemplate() { return textureTemplate; }

        GenericCacheTemplate& getCacheTemplate() { return cacheTemplate; }

        ShaderTemplate& getShaderTemplate() { return shaderTemplate; }
        FontTemplate& getFontTemplate() { return fontTemplate; }
        LogTemplate& getLogTemplate() { return logTemplate; }

        // Persist every staged template (regions + world metadata) in one pass.
        void flush() {
            regionTemplate.flush();
            worldMetadataTemplate.flush();
        }

    private:
        ChunkTemplate chunkTemplate;
        RegionTemplate regionTemplate;
        WorldMetadataTemplate worldMetadataTemplate;

        TextureTemplate textureTemplate;
        ModelTemplate modelTemplate;

        GenericCacheTemplate cacheTemplate;

        ShaderTemplate shaderTemplate;
        FontTemplate fontTemplate;
        LogTemplate logTemplate;
    };
}
