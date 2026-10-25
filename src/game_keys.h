// OutRun Online Arcade keyboard layout (used by game_menu.cpp).
// Up/Down drive the triggers (accelerate/brake) and also tilt the stick so
// menus still scroll; Left/Right steer. Other buttons keep their controller
// names - rebind them from F1 > Edit keyboard keys.
#pragma once

namespace recomp_ui::game_keys {

struct KeyBind {
  const char* cvar;
  const char* label;
  const char* default_key;
};

inline const KeyBind kKeyBinds[] = {
    {"keybind_lstick_left", "Steer left (stick)", "Left"},
    {"keybind_lstick_right", "Steer right (stick)", "Right"},
    {"keybind_lstick_up", "Stick up", "Up"},
    {"keybind_lstick_down", "Stick down", "Down"},
    {"keybind_right_trigger", "Accelerate (RT)", "Up"},
    {"keybind_left_trigger", "Brake (LT)", "Down"},
    {"keybind_a", "A button", "Z"},
    {"keybind_b", "B button", "X"},
    {"keybind_x", "X button", "A"},
    {"keybind_y", "Y button", "S"},
    {"keybind_left_shoulder", "Left bumper (LB)", "Q"},
    {"keybind_right_shoulder", "Right bumper (RB)", "W"},
    {"keybind_dpad_up", "D-pad up", "I"},
    {"keybind_dpad_down", "D-pad down", "K"},
    {"keybind_dpad_left", "D-pad left", "J"},
    {"keybind_dpad_right", "D-pad right", "L"},
    {"keybind_start", "Start / pause", "Return"},
    {"keybind_back", "Back", "Backspace"},
};

inline const char* const kUnbound[] = {"keybind_lstick_press", "keybind_rstick_press",
                                       "keybind_guide"};

}  // namespace recomp_ui::game_keys
