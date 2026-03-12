#include "node_content_renderer.h"
#include "editor/document.h"
#include "editor/window/blueprint_window.h"
#include "editor/visual/node/widget.h"
#include <imgui.h>

namespace an24 {

void NodeContentRenderer::render(Document& doc, BlueprintWindow& win, Pt cmin) {
    float zoom = win.scene.viewport().zoom;
    
    for (auto& node : doc.blueprint().nodes) {
        if (node.group_id != win.scene.groupId()) continue;

        auto* visual = win.scene.cache().getOrCreate(node, doc.blueprint().wires);
        visual->setPosition(node.pos);
        node.size = visual->getSize();

        Pt screen_min = win.scene.viewport().world_to_screen(visual->getPosition(), cmin);
        NodeContentType ctype = visual->getContentType();
        if (ctype == NodeContentType::None) continue;

        Bounds cb = visual->getContentBounds();
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
    float range = content.max - content.min;
    float progress = (range > 1e-6f) ? (content.value - content.min) / range : 0.0f;
    progress = std::max(0.0f, std::min(1.0f, progress));
    ImGui::ProgressBar(progress, ImVec2(width, 0));
}

void NodeContentRenderer::renderText(const NodeContent& content) {
    ImGui::Text("%s", content.label.c_str());
}

bool NodeContentRenderer::isHoldButton(const Node& node) const {
    return node.type_name == "HoldButton";
}

} // namespace an24
