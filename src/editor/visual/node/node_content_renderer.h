#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include "ui/math/pt.h"
#include "editor/document.h"
#include "editor/window/blueprint_window.h"
#include <functional>


/// Renders node content widgets (gauges, switches, sliders, text)
/// Follows SRP - only handles node content rendering, not canvas/grid/wires
class NodeContentRenderer {
public:
    using HoldButtonCallback = std::function<void(const std::string& nodeId, bool pressed)>;
    
    /// Render content for all nodes in the current group
    void render(Document& doc, BlueprintWindow& win, Pt cmin);
    
    /// Set callback for hold button press/release (for testing/decoupling)
    void setHoldButtonCallback(HoldButtonCallback cb) { holdButtonCallback_ = std::move(cb); }

private:
    static constexpr float MIN_CONTENT_WIDTH = 20.0f;
    
    HoldButtonCallback holdButtonCallback_;
    
    void renderSwitch(const bp2::Blueprint::Node& node, float width, bool readOnly, Document& doc);
    void renderValue(const bp2::Blueprint::Node& node, float width, bool readOnly);
    void renderGauge(const bp2::Blueprint::Node& node, float width);
    void renderText(const bp2::Blueprint::Node& node);
    
    bool isHoldButton(const bp2::Blueprint::Node& node, const ui::StringInterner& interner) const;
};
