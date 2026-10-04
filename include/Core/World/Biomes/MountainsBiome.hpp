#pragma once
#include "Biome.hpp"

namespace kc {
    class MountainsBiome : public Biome {
    public:
        MountainsBiome();
        bool useBareRock(int worldTopY, float dither, const Climate& climate) const override;
    };
}