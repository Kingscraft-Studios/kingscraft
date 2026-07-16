#pragma once

namespace lve {

class Window;
class Device;
class Renderer;
class UiWrapper;
class KeyBindHandler;
class TextureCache;
class World;
class ProfilingCapture;

class AppContext {
public:
    static AppContext& get() {
        static AppContext instance;
        return instance;
    }

    Device* device = nullptr;
    Renderer* renderer = nullptr;
    UiWrapper* uiSystem = nullptr;
    KeyBindHandler* keybinds = nullptr;
    TextureCache* textureCache = nullptr;
    World* world = nullptr;
    ProfilingCapture* profilingCapture = nullptr;

private:
    AppContext() = default;
};

} // namespace lve
