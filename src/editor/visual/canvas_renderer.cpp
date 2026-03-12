#include "canvas_renderer.h"
#include "editor/visual/renderer/blueprint_renderer.h"
#include "editor/imgui_draw_list.h"
#include "editor/input/input_types.h"
#include "editor/input/key_handler.h"
#include <imgui.h>

static ImGuiDrawList make_dl(ImDrawList* raw) {
    ImGuiDrawList dl;
    dl.dl = raw;
    return dl;
}

void CanvasRenderer::render(BlueprintWindow& win, Document& doc, WindowSystem& ws,
                            Pt cmin, Pt cmax, ImDrawList* draw_list, bool hovered) {
    auto dl = make_dl(draw_list);
    
    if (hovered) {
        ImVec2 mp = ImGui::GetMousePos();
        Pt mouse_world = win.scene.viewport().screen_to_world(Pt(mp.x, mp.y), cmin);
        win.input.update_hover(mouse_world);
    } else {
        win.input.update_hover(Pt(CanvasConstants::HOVER_CLEAR_X, CanvasConstants::HOVER_CLEAR_Y));
    }

    renderGrid(win, cmin, cmax, draw_list);
    renderBlueprint(win, doc, cmin, cmax, draw_list);
    renderTooltips(win, doc, cmin, draw_list);
    renderTempWire(win, cmin, draw_list);
    node_renderer_.render(doc, win, cmin);
    renderMarquee(win, cmin, draw_list);
    
    if (hovered) {
        handleInput(win, doc, ws, cmin);
    }
}

void CanvasRenderer::renderGrid(BlueprintWindow& win, Pt cmin, Pt cmax, ImDrawList* draw_list) {
    auto dl = make_dl(draw_list);
    BlueprintRenderer::renderGrid(dl, win.scene.viewport(), cmin, cmax);
}

void CanvasRenderer::renderBlueprint(BlueprintWindow& win, Document& doc, Pt cmin, Pt cmax, ImDrawList* draw_list) {
    auto dl = make_dl(draw_list);
    win.scene.render(dl, cmin, cmax,
                     &win.input.selected_nodes(), win.input.selected_wire(),
                     &doc.simulation(), win.input.hovered_wire());
}

void CanvasRenderer::renderTooltips(BlueprintWindow& win, Document& doc, Pt cmin, ImDrawList* draw_list) {
    auto dl = make_dl(draw_list);
    
    ImVec2 mp = ImGui::GetMousePos();
    Pt world = win.scene.viewport().screen_to_world(Pt(mp.x, mp.y), cmin);
    auto tooltip = win.scene.detectTooltip(world, doc.simulation(), cmin);
    BlueprintRenderer::renderTooltip(dl, tooltip);
}

void CanvasRenderer::renderTempWire(BlueprintWindow& win, Pt cmin, ImDrawList* draw_list) {
    if (!win.input.has_temp_wire()) return;
    
    Pt start_world = win.input.temp_wire_start();
    Pt end_world = win.input.temp_wire_end_world();
    Pt s = win.scene.viewport().world_to_screen(start_world, cmin);
    Pt e = win.scene.viewport().world_to_screen(end_world, cmin);
    uint32_t color = win.input.is_reconnecting()
        ? CanvasColors::TEMP_WIRE_RECONNECT
        : CanvasColors::TEMP_WIRE_NEW;
    draw_list->AddLine(ImVec2(s.x, s.y), ImVec2(e.x, e.y), color, 2.0f);
}

void CanvasRenderer::renderMarquee(BlueprintWindow& win, Pt cmin, ImDrawList* draw_list) {
    if (!win.input.is_marquee_selecting()) return;
    
    auto dl = make_dl(draw_list);
    
    Pt ms = win.scene.viewport().world_to_screen(win.input.marquee_start(), cmin);
    Pt me = win.scene.viewport().world_to_screen(win.input.marquee_end(), cmin);
    Pt rmin(std::min(ms.x, me.x), std::min(ms.y, me.y));
    Pt rmax(std::max(ms.x, me.x), std::max(ms.y, me.y));
    dl.add_rect_filled(rmin, rmax, CanvasColors::MARQUEE_FILL);
    dl.add_rect(rmin, rmax, CanvasColors::MARQUEE_BORDER, 1.0f);
}

void CanvasRenderer::handleInput(BlueprintWindow& win, Document& doc, WindowSystem& ws, Pt cmin) {
    ImGuiIO& io = ImGui::GetIO();
    
    Modifiers mods;
    mods.alt  = io.KeyAlt;
    mods.ctrl = io.KeyCtrl || io.KeySuper;

    ImVec2 mp = ImGui::GetMousePos();
    Pt screen_pos(mp.x, mp.y);

    // Dispatch helper: apply input result to document, then let WindowSystem handle the action.
    auto dispatch = [&](InputResult result) {
        auto action = doc.applyInputResult(result, win.group_id);
        ws.handleInputAction(action, doc);
    };

    if (io.MouseWheel != 0.0f) {
        dispatch(win.input.on_scroll(io.MouseWheel * CanvasConstants::SCROLL_ZOOM_FACTOR, screen_pos, cmin));
    }

    bool was_dbl = false;
    if (ImGui::IsMouseDoubleClicked(0)) {
        dispatch(win.input.on_double_click(screen_pos, cmin));
        was_dbl = true;
    }

    if (!was_dbl && ImGui::IsMouseClicked(0)) {
        dispatch(win.input.on_mouse_down(screen_pos, MouseButton::Left, cmin, mods));
    }

    if (ImGui::IsMouseClicked(1)) {
        dispatch(win.input.on_mouse_down(screen_pos, MouseButton::Right, cmin, mods));
    }

    if (ImGui::IsMouseDragging(0)) {
        ImVec2 delta = ImGui::GetMouseDragDelta(0);
        dispatch(win.input.on_mouse_drag(MouseButton::Left, Pt(delta.x, delta.y), cmin));
        ImGui::ResetMouseDragDelta(0);
    }

    if (ImGui::IsMouseReleased(0)) {
        dispatch(win.input.on_mouse_up(MouseButton::Left, screen_pos, cmin));
    }

    key_handler::process_keys(io.WantCaptureKeyboard, win.read_only,
        [&](Key k) { dispatch(win.input.on_key(k)); });
}

