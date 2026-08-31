#include "Resource/ResourceManager.hpp"
#include "Threads/IO.hpp"
#include "Bus/MessageBus.hpp"
#include "stb_image.h"

namespace kc {

ResourceManager::ResourceManager(Device& device)
    : device_(device) {
}

void ResourceManager::loadTexture(const std::string& path,
    std::function<void(Texture*)> callback) {
    auto it = textures_.find(path);
    if (it != textures_.end()) {
        if (callback) callback(it->second.get());
        return;
    }

    MessageBus::Get().request<std::vector<char>>(
        ThreadName::Engine,
        [path]() { return IO::Get().readFile(path); },
        ThreadName::Renderer,
        [this, path, callback = std::move(callback)](std::vector<char> fileData) mutable {
            auto texture = std::make_unique<Texture>(device_, fileData);
            auto* ptr = texture.get();
            textures_[path] = std::move(texture);
            if (callback) callback(ptr);
        }
    );
}

void ResourceManager::loadRawImageData(const std::string& path,
    std::function<void(unsigned char* pixels, int width, int height)> callback) {
    MessageBus::Get().request<std::vector<char>>(
        ThreadName::Engine,
        [path]() { return IO::Get().readFile(path); },
        ThreadName::Renderer,
        [callback = std::move(callback)](std::vector<char> fileData) mutable {
            int width, height, channels;
            unsigned char* pixels = stbi_load_from_memory(
                reinterpret_cast<const stbi_uc*>(fileData.data()),
                fileData.size(), &width, &height, &channels, 4);
            if (pixels) {
                if (callback) callback(pixels, width, height);
                stbi_image_free(pixels);
            }
        }
    );
}

void ResourceManager::garbageCollect() {
    // Future: release textures with no external references
}

} // namespace kc
