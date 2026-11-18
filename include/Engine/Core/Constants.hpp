#pragma once

#include <glm/glm.hpp>

namespace kc {

    // Some Misc Constants
    constexpr int MAX_FRAMES_IN_FLIGHT = 2;
    constexpr int DEFAULT_WINDOW_WIDTH = 900;
    constexpr int DEFAULT_WINDOW_HEIGHT = 650;

    // Noesis Buton Names
    constexpr const char* BTN_ENTER_WORLD = "EnterWorldButton";
    constexpr const char* BTN_QUIT_GAME   = "QuitButton";
    constexpr const char* BTN_RESPAWN     = "RespawnButton";

    // Color palette (MainMenu.xaml inspired)
    constexpr glm::vec4 GOLD{0.831f, 0.686f, 0.216f, 1.0f};         // #D4AF37
    constexpr glm::vec4 GOLD_DARK{0.749f, 0.643f, 0.447f, 1.0f};    // #BFA472
    constexpr glm::vec4 BG_PURPLE{0.188f, 0.039f, 0.322f, 1.0f};    // #300A52
    constexpr glm::vec4 BG_DARK{0.020f, 0.008f, 0.031f, 1.0f};      // #050208
    constexpr glm::vec4 TRANSPARENT{0.0f, 0.0f, 0.0f, 0.0f};

    // Death screen palette
    constexpr glm::vec4 DEATH_TINT{0.55f, 0.02f, 0.02f, 0.40f};
    constexpr glm::vec4 DEATH_RED{0.85f, 0.12f, 0.12f, 1.0f};
    constexpr glm::vec4 BLOOD_DARK{0.35f, 0.02f, 0.02f, 1.0f};
    constexpr glm::vec4 BLOOD_DARK_TRANSP{0.35f, 0.02f, 0.02f, 0.85f};

} // namespace kc
