#pragma once

#include "Core/Resources/BlockModel.hpp"
#include "Core/World/Physics/AABB.hpp"

namespace lve {

class Block {
protected:
    BlockModel model_;
    int textureBaseOffset_ = 0;
    mutable AABB collisionBox_{glm::vec3(0.0f), glm::vec3(1.0f)};
    mutable bool collisionBoxReady_ = false;

    virtual AABB setCollisionBox() const = 0;
public:
    Block() = default;
    explicit Block(BlockModel model) : model_(std::move(model)) {}
    virtual ~Block() = default;

    const BlockModel& getModel() const { return model_; }

    int getTextureBaseOffset() const { return textureBaseOffset_; }
    void setTextureBaseOffset(int offset) { textureBaseOffset_ = offset; }

    const AABB& getCollisionBox() const {
        if (!collisionBoxReady_) {
            collisionBox_ = setCollisionBox();
            collisionBoxReady_ = true;
        }
        return collisionBox_;
    }

    virtual bool isSolid() const { return true; }
    virtual bool isTransparent() const { return false; }
    virtual bool isLiquid() const { return false; }
    virtual bool isReplaceable() const { return false; }
    virtual int getLightEmission() const { return 0; }
    virtual int getLightAbsorption() const { return isTransparent() ? 0 : 15; }
    virtual float getHardness() const { return 1.0f; }
};

} // namespace lve
