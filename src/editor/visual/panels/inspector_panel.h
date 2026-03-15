#pragma once

#include "editor/visual/inspector/inspector.h"
#include "editor/visual/layout/splitter.h"
#include "editor/window_system.h"
#include <memory>
#include <string>


/// Inspector panel with window setup, splitter, and selection handling.
class InspectorPanel {
public:
    struct Result {
        std::string selected_node_id;
    };
    
    InspectorPanel();
    
    Result render(::WindowSystem& ws, float menu_height, float available_height, float available_width);
    
    float totalWidth() const { return visible_ ? width_ + splitter_thickness_ : 0.0f; }
    bool visible() const { return visible_; }
    void setVisible(bool v) { visible_ = v; }

private:
    bool visible_ = true;
    float width_ = 280.0f;
    float splitter_thickness_ = 4.0f;
    std::unique_ptr<PanelSplitter> splitter_;
};

