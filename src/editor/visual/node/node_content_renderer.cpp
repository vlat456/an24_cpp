#include "node_content_renderer.h"
#include "editor/document.h"
#include "editor/identity.h"
#include "editor/window/blueprint_window.h"
#include "editor/visual/node/visual_node.h"
#include <imgui.h>


void NodeContentRenderer::render(Document& doc, BlueprintWindow& win, Pt cmin) {
    float zoom = win.viewport.zoom;
    const auto& interner = doc.interner();

    // Choose the correct blueprint source: inline_def for embedded, root for others
    const bp2::Blueprint& bp = win.rendered_blueprint();

    for (const auto& node : bp.nodes()) {
        if (win.resolved_scope_id().is_embedded()) {
            if (!node.layout.layout_group.empty()) continue;
        }

        // Find the corresponding widget in the scene tree
        std::string_view node_id_sv = interner.resolve(node.semantic.id);
        auto* widget = win.scene.find(node_id_sv);
        if (!widget) continue;
        auto* node_widget = dynamic_cast<visual::NodeWidget*>(widget);
        if (!node_widget) continue;

        bp2::NodeContentType ctype = node.view.content_type;
        if (ctype == bp2::NodeContentType::None) continue;

        Pt screen_min = win.viewport.world_to_screen(node_widget->worldPos(), cmin);
        Bounds cb = node_widget->contentBounds();
        float cx = screen_min.x + cb.x * zoom;
        float cy = screen_min.y + cb.y * zoom;
        float aw = cb.w * zoom;
        if (aw <= MIN_CONTENT_WIDTH) continue;

        ImGui::SetCursorScreenPos(ImVec2(cx, cy));

        switch (node.view.content_type) {
            case bp2::NodeContentType::Switch:
                renderSwitch(node, aw, win.read_only, doc, win.resolved_scope_id().key());
                break;
            case bp2::NodeContentType::Value:
                renderValue(node, aw, win.read_only);
                break;
            case bp2::NodeContentType::Gauge:
                renderGauge(node, aw);
                break;
            case bp2::NodeContentType::Text:
                renderText(node);
                break;
            default:
                break;
        }
    }
}

void NodeContentRenderer::renderSwitch(const bp2::Blueprint::Node& node,
                                        float width, bool readOnly,
                                        Document& doc, const std::string& scope_id) {
    if (readOnly) return;
    
    if (isHoldButton(node, doc.interner())) {
        bool checked = node.view.content_state;
        // Resolve node.semantic.id (InternedId) to string for ImGui ID and callbacks
        std::string node_id_str(doc.interner().resolve(node.semantic.id));
        std::string id = "##hold_" + node_id_str;
        if (ImGui::Checkbox(id.c_str(), &checked)) {
            auto typed_nid = editor::NodeId::from_string(node_id_str);
            if (holdButtonCallback_) {
                holdButtonCallback_(node_id_str, checked);
            } else {
                if (checked) doc.holdButtonPress(typed_nid, scope_id);
                else doc.holdButtonRelease(typed_nid, scope_id);
            }
        }
    }
}

void NodeContentRenderer::renderValue(const bp2::Blueprint::Node& node,
                                       float width, bool readOnly) {
    if (readOnly) return;
    // node.view.content_value is const in the blueprint; this renderer is read-only display
    // (mutations go through EditorModel). Just show the slider as read-only.
    float val = node.view.content_value;
    ImGui::SetNextItemWidth(width);
    std::string id = "##v_" + std::to_string(node.semantic.id.raw());
    ImGui::SliderFloat(id.c_str(), &val, node.view.content_min, node.view.content_max, "%.2f");
}

void NodeContentRenderer::renderGauge(const bp2::Blueprint::Node& node, float width) {
    // Gauge is now rendered by VoltmeterWidget in the scene graph.
    // No ImGui overlay needed — the analog needle gauge replaces
    // the old progress bar.
    (void)node;
    (void)width;
}

void NodeContentRenderer::renderText(const bp2::Blueprint::Node& node) {
    ImGui::Text("%s", node.view.content_label.c_str());
}

bool NodeContentRenderer::isHoldButton(const bp2::Blueprint::Node& node,
                                        const ui::StringInterner& interner) const {
    std::string_view type_sv = interner.resolve(node.semantic.type);
    return type_sv == "HoldButton";
}
