#include "node_content_renderer.h"
#include "editor/document.h"
#include "editor/window/blueprint_window.h"
#include "editor/visual/node/visual_node.h"
#include <imgui.h>


void NodeContentRenderer::render(Document& doc, BlueprintWindow& win, Pt cmin) {
    float zoom = win.viewport.zoom;
    
    for (auto& node : doc.blueprint().nodes) {
        if (node.group_id != win.group_id) continue;

        // Find the corresponding widget in the scene tree
        auto* widget = win.scene.find(node.id);
        if (!widget) continue;
        auto* node_widget = dynamic_cast<visual::NodeWidget*>(widget);
        if (!node_widget) continue;

        NodeContentType ctype = node.node_content.type;
        if (ctype == NodeContentType::None) continue;

        Pt screen_min = win.viewport.world_to_screen(node_widget->worldPos(), cmin);
        Bounds cb = node_widget->contentBounds();
        float cx = screen_min.x + cb.x * zoom;
        float cy = screen_min.y + cb.y * zoom;
        float aw = cb.w * zoom;
        if (aw <= MIN_CONTENT_WIDTH) continue;

        ImGui::SetCursorScreenPos(ImVec2(cx, cy));
        NodeContent& content = node.node_content;

        switch (content.type) {
            case NodeContentType::Switch:
                renderSwitch(node, content, aw, win.read_only, doc);
                break;
            case NodeContentType::Value:
                renderValue(content, aw, win.read_only);
                break;
            case NodeContentType::Gauge:
                renderGauge(content, aw);
                break;
            case NodeContentType::Text:
                renderText(content);
                break;
            default:
                break;
        }
    }
}

void NodeContentRenderer::renderSwitch(const Node& node, NodeContent& content, 
                                        float width, bool readOnly, Document& doc) {
    if (readOnly) return;
    
    if (isHoldButton(node)) {
        bool checked = content.state;
        std::string id = "##hold_" + node.id;
        if (ImGui::Checkbox(id.c_str(), &checked)) {
            if (holdButtonCallback_) {
                holdButtonCallback_(node.id, checked);
            } else {
                if (checked) doc.holdButtonPress(node.id);
                else doc.holdButtonRelease(node.id);
            }
        }
    }
}

void NodeContentRenderer::renderValue(NodeContent& content, float width, bool readOnly) {
    if (readOnly) return;
    ImGui::SetNextItemWidth(width);
    std::string id = "##v_" + std::to_string(reinterpret_cast<uintptr_t>(&content));
    ImGui::SliderFloat(id.c_str(), &content.value, content.min, content.max, "%.2f");
}

void NodeContentRenderer::renderGauge(const NodeContent& content, float width) {
    // Gauge is now rendered by VoltmeterWidget in the scene graph.
    // No ImGui overlay needed — the analog needle gauge replaces
    // the old progress bar.
    (void)content;
    (void)width;
}

void NodeContentRenderer::renderText(const NodeContent& content) {
    ImGui::Text("%s", content.label.c_str());
}

bool NodeContentRenderer::isHoldButton(const Node& node) const {
    return node.type_name == "HoldButton";
}

