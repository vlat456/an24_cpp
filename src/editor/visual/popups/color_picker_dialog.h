#pragma once

#include "editor/window_system.h"
#include <imgui.h>

namespace an24 {

/// Color picker dialog
class ColorPickerDialog {
public:
    void render(WindowSystem& ws);
    
private:
    bool showing_ = false;
    
    void show() { showing_ = true; }
    void hide() { showing_ = false; }
};

} // namespace an24
