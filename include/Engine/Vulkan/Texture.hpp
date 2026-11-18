#pragma once

#include "Device.hpp"
#include "Image.hpp"
#include "ImageView.hpp"
#include "Sampler.hpp"

#include <memory>
#include <string>
#include <vector>

namespace kc {

    class Texture {
    public:
        Texture(Device &device, const std::vector<char>& fileData);

        Texture(const Texture&) = delete;
        Texture& operator=(const Texture&) = delete;

        VkImageView getImageView() const { return imageView_->getHandle(); }

        VkSampler getSampler() const { return sampler_->getHandle(); }

    private:
        void createTextureImage(const std::vector<char>& fileData);

        void createTextureImageView();

        void createTextureSampler();

        Device &device;

        std::unique_ptr<Image> image_;
        std::unique_ptr<ImageView> imageView_;
        std::unique_ptr<Sampler> sampler_;
    };

}  // namespace kc
