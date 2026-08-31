#pragma once

#include "Device.hpp"
#include <string>
#include <vector>

namespace kc {

    class Texture {
    public:
        Texture(Device &device, const std::vector<char>& fileData);

        ~Texture();

        VkImageView getImageView() const { return textureImageView; }

        VkSampler getSampler() const { return textureSampler; }

    private:
        void createTextureImage(const std::vector<char>& fileData);

        void createTextureImageView();

        void createTextureSampler();

        Device &device;

        VkImage textureImage;
        VkDeviceMemory textureImageMemory;
        VkImageView textureImageView;
        VkSampler textureSampler;
    };

}  // namespace kc
