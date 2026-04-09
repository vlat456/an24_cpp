#include "color_picker_dialog.h"
#include "editor/window_system.h"
#include "editor/document.h"
#include "editor/commands/commands.h"
#include <imgui.h>
#include <cstdint>


/// Look up the visual widget for a node in the correct window's scene.
static visual::Widget* find_visual_widget(Document& doc,
                                          const std::string& node_id,
                                          const std::string& scope_id) {
    BlueprintWindow* win = doc.windowManager().find(scope_id);
    if (!win) return nullptr;
    return win->scene.find(node_id);
}

/// Pack four [0,1] floats into an ImGui ABGR uint32.
static uint32_t pack_color(float r, float g, float b, float a) {
    auto clamp01 = [](float v) -> float {
        return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    };
    uint8_t ri = static_cast<uint8_t>(clamp01(r) * 255.0f + 0.5f);
    uint8_t gi = static_cast<uint8_t>(clamp01(g) * 255.0f + 0.5f);
    uint8_t bi = static_cast<uint8_t>(clamp01(b) * 255.0f + 0.5f);
    uint8_t ai = static_cast<uint8_t>(clamp01(a) * 255.0f + 0.5f);
    return (uint32_t(ai) << 24) | (uint32_t(bi) << 16) | (uint32_t(gi) << 8) | uint32_t(ri);
}

void ColorPickerDialog::render(WindowSystem& ws) {
    if (ws.colorPicker.show) {
        ImGui::OpenPopup("Node Color");
        ws.colorPicker.show = false;
    }

    if (ImGui::BeginPopupModal("Node Color", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        Document* doc = ws.findDocumentById(ws.colorPicker.source_doc_id);
        if (!doc) doc = ws.activeDocument();

        ui::InternedId node_iid = doc
            ? doc->interner().lookup(ws.colorPicker.node_id.str())
            : ui::InternedId{};
        const bp2::Blueprint::Node* node_ptr = (doc && !node_iid.empty())
            ? doc->blueprint().find_node(node_iid)
            : nullptr;

        if (doc && node_ptr) {
            // NOTE: Do NOT overwrite rgba[] from node color here.
            // openColorPickerForNode() already initialised rgba[] once.
            // Overwriting every frame would fight the ImGui picker and
            // cause the selected value to snap back to the original color.

            ImGui::ColorPicker4("##picker", ws.colorPicker.rgba,
                ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_DisplayRGB);

            if (ImGui::Button("Apply")) {
                float r = ws.colorPicker.rgba[0];
                float g = ws.colorPicker.rgba[1];
                float b = ws.colorPicker.rgba[2];
                float a = ws.colorPicker.rgba[3];

                doc->model().push_checkpoint();
                execute(doc->model(), doc->interner(),
                        cmd_set_color(node_iid, true, r, g, b, a));

                // Update visual widget immediately so the color change is
                // visible without requiring a blueprint reload.
                if (auto* w = find_visual_widget(*doc, ws.colorPicker.node_id.str(),
                                                  ws.colorPicker.scope_id)) {
                    w->setCustomColor(pack_color(r, g, b, a));
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset")) {
                doc->model().push_checkpoint();
                execute(doc->model(), doc->interner(),
                        cmd_set_color(node_iid, false, 0.5f, 0.5f, 0.5f, 1.0f));

                // Clear custom color on the visual widget immediately.
                if (auto* w = find_visual_widget(*doc, ws.colorPicker.node_id.str(),
                                                  ws.colorPicker.scope_id)) {
                    w->setCustomColor(std::nullopt);
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
}
