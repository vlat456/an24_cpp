#include "canvas_renderer.h"
#include "editor/window_system.h"
#include "editor/visual/renderer/blueprint_renderer.h"
#include "editor/imgui_draw_list.h"
#include "editor/input/input_types.h"
#include <imgui.h>

namespace an24 {

void CanvasRenderer::render(BlueprintWindow& win, Document& doc, WindowSystem& ws,
                            Pt cmin, Pt cmax, ImDrawList* draw_list, bool hovered) {
    renderGrid(win, cmin, cmax);
    renderBlueprint(win, doc, cmin, cmax);
    renderTooltips(win, doc, cmin);
    renderTempWire(win, cmin);
    renderNodeContent(win, doc, cmin);
    renderMarquee(win, cmin);
    
    if (hovered) {
        handleInput(win, doc, ws, cmin);
    }
}

void CanvasRenderer::renderGrid(BlueprintWindow& win, Pt cmin, Pt cmax) {
    ImGuiDrawList dl;
    dl.dl = ImGui::GetWindowDrawList();
    BlueprintRenderer::renderGrid(dl, win.scene.viewport(), cmin, cmax);
}

void CanvasRenderer::renderBlueprint(BlueprintWindow& win, Document& doc, Pt cmin, Pt cmax) {
    ImGuiDrawList dl;
    dl.dl = ImGui::GetWindowDrawList();
    win.scene.render(dl, cmin, cmax,
                     &win.input.selected_nodes(), win.input.selected_wire(),
                     &doc.simulation(), win.input.hovered_wire());
}

void CanvasRenderer::renderTooltips(BlueprintWindow& win, Document& doc, Pt cmin) {
    ImGuiDrawList dl;
    dl.dl = ImGui::GetWindowDrawList();
    
    ImVec2 mp = ImGui::GetMousePos();
    Pt world = win.scene.viewport().screen_to_world(Pt(mp.x, mp.y), cmin);
    auto tooltip = win.scene.detectTooltip(world, doc.simulation(), cmin);
    BlueprintRenderer::renderTooltip(dl, tooltip);
}

void CanvasRenderer::renderTempWire(BlueprintWindow& win, Pt cmin) {
    if (!win.input.has_temp_wire()) return;
    
    ImGuiDrawList dl;
    dl.dl = ImGui::GetWindowDrawList();
    
    Pt start_world = win.input.temp_wire_start();
    Pt end_world = win.input.temp_wire_end_world();
    Pt s = win.scene.viewport().world_to_screen(start_world, cmin);
    Pt e = win.scene.viewport().world_to_screen(end_world, cmin);
    uint32_t color = win.input.is_reconnecting()
        ? IM_COL32(255, 150, 50, 200)
        : IM_COL32(255, 255, 100, 200);
    dl.dl->AddLine(ImVec2(s.x, s.y), ImVec2(e.x, e.y), color, 2.0f);
}

void CanvasRenderer::renderNodeContent(BlueprintWindow& win, Document& doc, Pt cmin) {
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
        if (aw <= 20.0f) continue;

        ImGui::SetCursorScreenPos(ImVec2(cx, cy));
        NodeContent& content = node.node_content;

        switch (content.type) {
            case NodeContentType::Switch: {
                if (!win.read_only && node.type_name == "HoldButton") {
                    bool checked = content.state;
                    std::string id = "##hold_" + node.id;
                    if (ImGui::Checkbox(id.c_str(), &checked)) {
                        if (checked) doc.holdButtonPress(node.id);
                        else doc.holdButtonRelease(node.id);
                    }
                }
                break;
            }
            case NodeContentType::Value: {
                if (!win.read_only) {
                    ImGui::SetNextItemWidth(aw);
                    std::string id = "##v_" + node.id;
                    ImGui::SliderFloat(id.c_str(), &content.value, content.min, content.max, "%.2f");
                }
                break;
            }
            case NodeContentType::Gauge: {
                float range = content.max - content.min;
                float progress = (range > 1e-6f) ? (content.value - content.min) / range : 0.0f;
                progress = std::max(0.0f, std::min(1.0f, progress));
                ImGui::ProgressBar(progress, ImVec2(aw, 0));
                break;
            }
            case NodeContentType::Text:
                ImGui::Text("%s", content.label.c_str());
                break;
            default: break;
        }
    }
}

void CanvasRenderer::renderMarquee(BlueprintWindow& win, Pt cmin) {
    if (!win.input.is_marquee_selecting()) return;
    
    ImGuiDrawList dl;
    dl.dl = ImGui::GetWindowDrawList();
    
    Pt ms = win.scene.viewport().world_to_screen(win.input.marquee_start(), cmin);
    Pt me = win.scene.viewport().world_to_screen(win.input.marquee_end(), cmin);
    Pt rmin(std::min(ms.x, me.x), std::min(ms.y, me.y));
    Pt rmax(std::max(ms.x, me.x), std::max(ms.y, me.y));
    dl.add_rect_filled(rmin, rmax, 0x4000FF00);
    dl.add_rect(rmin, rmax, 0xFF00FF00, 1.0f);
}

void CanvasRenderer::handleInput(BlueprintWindow& win, Document& doc, WindowSystem& ws, Pt cmin) {
    ImGuiIO& io = ImGui::GetIO();
    
    Modifiers mods;
    mods.alt  = io.KeyAlt;
    mods.ctrl = io.KeyCtrl || io.KeySuper;

    ImVec2 mp = ImGui::GetMousePos();
    Pt screen_pos(mp.x, mp.y);

    // Scroll -> zoom
    if (io.MouseWheel != 0.0f) {
        auto action = doc.applyInputResult(win.input.on_scroll(io.MouseWheel * 10.0f, screen_pos, cmin), win.group_id);
        ws.handleInputAction(action, doc);
    }

    // Double-click
    bool was_dbl = false;
    if (ImGui::IsMouseDoubleClicked(0)) {
        auto action = doc.applyInputResult(win.input.on_double_click(screen_pos, cmin), win.group_id);
        ws.handleInputAction(action, doc);
        was_dbl = true;
    }

    // Left click
    if (!was_dbl && ImGui::IsMouseClicked(0)) {
        auto action = doc.applyInputResult(win.input.on_mouse_down(screen_pos, MouseButton::Left, cmin, mods), win.group_id);
        ws.handleInputAction(action, doc);
    }

    // Right click
    if (ImGui::IsMouseClicked(1)) {
        auto action = doc.applyInputResult(win.input.on_mouse_down(screen_pos, MouseButton::Right, cmin, mods), win.group_id);
        ws.handleInputAction(action, doc);
    }

    // Left drag
    if (ImGui::IsMouseDragging(0)) {
        ImVec2 delta = ImGui::GetMouseDragDelta(0);
        auto action = doc.applyInputResult(win.input.on_mouse_drag(MouseButton::Left, Pt(delta.x, delta.y), cmin), win.group_id);
        ws.handleInputAction(action, doc);
        ImGui::ResetMouseDragDelta(0);
    }

    // Left release
    if (ImGui::IsMouseReleased(0)) {
        auto action = doc.applyInputResult(win.input.on_mouse_up(MouseButton::Left, screen_pos, cmin), win.group_id);
        ws.handleInputAction(action, doc);
    }

    // Keyboard
    if (!io.WantCaptureKeyboard) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            auto action = doc.applyInputResult(win.input.on_key(Key::Escape), win.group_id);
            ws.handleInputAction(action, doc);
        }
        if (!win.read_only) {
            if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
                auto action = doc.applyInputResult(win.input.on_key(Key::Delete), win.group_id);
                ws.handleInputAction(action, doc);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
                auto action = doc.applyInputResult(win.input.on_key(Key::Backspace), win.group_id);
                ws.handleInputAction(action, doc);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_R)) {
                auto action = doc.applyInputResult(win.input.on_key(Key::R), win.group_id);
                ws.handleInputAction(action, doc);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket)) {
                auto action = doc.applyInputResult(win.input.on_key(Key::LeftBracket), win.group_id);
                ws.handleInputAction(action, doc);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_RightBracket)) {
                auto action = doc.applyInputResult(win.input.on_key(Key::RightBracket), win.group_id);
                ws.handleInputAction(action, doc);
            }
        }
    }
}

} // namespace an24
