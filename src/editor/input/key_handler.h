#pragma once

#include "editor/input/input_types.h"
#include <imgui.h>
#include <functional>

namespace key_handler {

struct KeyMapping {
    ImGuiKey imgui_key;
    Key app_key;
    bool requires_editable;
};

inline constexpr KeyMapping EDITOR_KEYS[] = {
    {ImGuiKey_Escape,     Key::Escape,      false},
    {ImGuiKey_Delete,     Key::Delete,      true},
    {ImGuiKey_Backspace,  Key::Backspace,   true},
    {ImGuiKey_R,          Key::R,           true},
    {ImGuiKey_LeftBracket,  Key::LeftBracket,  true},
    {ImGuiKey_RightBracket, Key::RightBracket, true},
};

inline constexpr size_t KEY_COUNT = sizeof(EDITOR_KEYS) / sizeof(KeyMapping);

template<typename OnKeyFn>
void process_keys(bool want_capture_keyboard, bool is_read_only, OnKeyFn&& on_key) {
    if (want_capture_keyboard) return;
    
    for (size_t i = 0; i < KEY_COUNT; i++) {
        const auto& mapping = EDITOR_KEYS[i];
        if (mapping.requires_editable && is_read_only) continue;
        if (ImGui::IsKeyPressed(mapping.imgui_key)) {
            on_key(mapping.app_key);
        }
    }
}

}
