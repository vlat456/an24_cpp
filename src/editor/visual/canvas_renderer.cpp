#include "canvas_renderer.h"
#include "editor/visual/renderer/grid_renderer.h"
#include "editor/visual/render_context.h"
#include "editor/visual/wire/wire.h"
#include "editor/visual/port/visual_port.h"
#include "editor/visual/scene_hittest.h"
#include "editor/visual/renderer/draw_list.h"
#include "editor/imgui_draw_list.h"
#include "editor/visual/oscilloscope_plot.h"
#include "editor/input/input_types.h"
#include "editor/input/key_handler.h"
#include "blueprint_v2/path/path.h"
#include <imgui.h>
#include <unordered_set>
#include <cstdio>

static void render_probe_markers(BlueprintWindow& win, Document& doc, WindowSystem& ws,
                                 Pt cmin, ImDrawList* draw_list) {
    if (!ws.showOscilloscope) return;
    for (const auto& [wire_id, probe] : ws.oscilloscope.probes()) {
        if (probe.doc_id != doc.id()) continue;
        if (probe.group_id != win.group_id) continue;

        Pt sp = win.viewport.world_to_screen(probe.world_pos, cmin);
        visual::osc::draw_probe_marker(draw_list, sp, probe.color);
    }
}

static void render_hover_scope_tooltip(Document& doc,
                                       WindowSystem& ws,
                                       const std::string& label,
                                       const Pt& anchor_screen) {
    const std::deque<float>& samples = ws.oscilloscope.hover_samples();
    if (samples.empty()) return;

    OscilloscopeProbe pseudo;
    pseudo.label = label;
    pseudo.wire_id = "hover_scope";
    pseudo.color = IM_COL32(80, 200, 255, 255);

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    constexpr float kWindowW = 380.0f;
    constexpr float kWindowH = 132.0f;
    float px = anchor_screen.x + 10.0f;
    float py = anchor_screen.y - (kWindowH + 10.0f);
    if (px + kWindowW > display.x - 8.0f) px = display.x - kWindowW - 8.0f;
    if (px < 8.0f) px = 8.0f;
    if (py < 8.0f) py = anchor_screen.y + 10.0f;
    if (py + kWindowH > display.y - 8.0f) py = display.y - kWindowH - 8.0f;

    ImGui::SetNextWindowBgAlpha(0.96f);
    ImGui::SetNextWindowPos(ImVec2(px, py), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kWindowW, kWindowH), ImGuiCond_Always);
    if (ImGui::Begin("##hover_scope", nullptr,
                     ImGuiWindowFlags_NoDecoration |
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoNav)) {
        std::vector<OscilloscopeModel::ChannelView> one;
        one.push_back({&pseudo, &samples});
        float min_v = 0.0f;
        float max_v = 0.0f;
        visual::osc::compute_range(one, min_v, max_v);
        visual::osc::render_channel_plot(pseudo, samples, min_v, max_v, 72.0f, -1.0f);
        visual::osc::render_stats_row(OscilloscopeModel::compute_stats(samples, ws.oscilloscope.sample_period_sec()));
    }
    ImGui::End();
}

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
        Pt mouse_world = win.viewport.screen_to_world(Pt(mp.x, mp.y), cmin);
        win.input.update_hover(mouse_world);
    } else {
        win.input.update_hover(Pt(CanvasConstants::HOVER_CLEAR_X, CanvasConstants::HOVER_CLEAR_Y));
    }

    renderGrid(win, cmin, cmax, draw_list);
    renderBlueprint(win, doc, cmin, cmax, draw_list);
    renderTooltips(win, doc, ws, cmin, draw_list);
    renderTempWire(win, cmin, draw_list);
    render_probe_markers(win, doc, ws, cmin, draw_list);
    node_renderer_.render(doc, win, cmin);
    renderMarquee(win, cmin, draw_list);
    
    if (hovered) {
        handleInput(win, doc, ws, cmin);
    }
}

void CanvasRenderer::renderGrid(BlueprintWindow& win, Pt cmin, Pt cmax, ImDrawList* draw_list) {
    auto dl = make_dl(draw_list);
    visual::GridRenderer grid;
    grid.render(dl, win.viewport, cmin, cmax);
}

void CanvasRenderer::renderBlueprint(BlueprintWindow& win, Document& doc, Pt cmin, Pt cmax, ImDrawList* draw_list) {
    auto dl = make_dl(draw_list);

    // Build energized wire set from simulation (reuse buffer across frames)
    energized_buf_.clear();
    doc.buildEnergizedWireSet(energized_buf_, win.group_id);

    // Resolve selected node IDs → pointers for this frame.
    // Must outlive ctx (which stores a pointer to it).
    auto sel_nodes = win.input.selected_nodes();

    visual::RenderContext ctx;
    ctx.zoom = win.viewport.zoom;
    ctx.pan = win.viewport.pan;
    ctx.canvas_min = cmin;
    ctx.selected_nodes = &sel_nodes;
    ctx.selected_wire = win.input.selected_wire();
    ctx.hovered_wire = win.input.hovered_wire();
    ctx.hovered_routing_point = win.input.hovered_routing_point();
    ctx.energized_wires = energized_buf_.empty() ? nullptr : &energized_buf_;

    visual::compute_wire_crossings(win.scene);
    win.scene.render(&dl, ctx);
}

void CanvasRenderer::renderTooltips(BlueprintWindow& win, Document& doc, WindowSystem& ws,
                                    Pt cmin, ImDrawList* draw_list) {
    (void)draw_list;
    if (!doc.isSimulationRunning()) return;

    ImVec2 mp = ImGui::GetMousePos();
    Pt mouse_screen(mp.x, mp.y);
    Pt mouse_world = win.viewport.screen_to_world(mouse_screen, cmin);

    auto hit = visual::hit_test(win.scene, mouse_world);
    ws.oscilloscope.clear_hover_signal();

    if (auto* hp = std::get_if<visual::HitPort>(&hit)) {
        visual::Port* port = hp->port;
        std::string_view node_id = port->rootAncestorId();
        if (node_id.empty()) return;

        std::string_view port_name = port->name();
        Pt port_screen = win.viewport.world_to_screen(port->worldPos(), cmin);
        const std::string signal_key = std::string(node_id) + "." + std::string(port_name);
        ws.oscilloscope.set_hover_signal(signal_key);
        render_hover_scope_tooltip(doc, ws, signal_key, port_screen);
        return;

    } else if (auto* hw = std::get_if<visual::HitWire>(&hit)) {
        visual::Wire* wire = hw->wire;
        std::string_view wire_id_sv = wire->id();
        auto& interner = doc.interner();
        auto wire_iid = interner.lookup(wire_id_sv);
        const bp2::Blueprint::Wire* data_wire = doc.blueprint().find_wire(wire_iid);
        if (!data_wire) return;

        // Decode the source port path: Port -> Node -> Root
        const auto& arena = doc.arena();
        bp2::Path src = data_wire->source;
        if (src.kind() != bp2::PathKind::Port) return;
        ui::InternedId port_iid = src.segment();
        bp2::Path node_path = arena.parent(src);
        if (node_path.kind() != bp2::PathKind::Node) return;
        ui::InternedId node_iid = node_path.segment();

        std::string_view node_sv = interner.resolve(node_iid);
        std::string_view port_sv = interner.resolve(port_iid);
        std::string signal_key;
        signal_key.reserve(node_sv.size() + 1 + port_sv.size());
        signal_key.append(node_sv);
        signal_key.push_back('.');
        signal_key.append(port_sv);
        // Project mouse onto wire segment for tooltip anchor
        const auto& poly = wire->polyline();
        size_t seg = hw->segment;
        Pt anchor = mouse_world;
        if (seg + 1 < poly.size()) {
            // Closest point on segment
            Pt a = poly[seg], b = poly[seg + 1];
            float dx = b.x - a.x, dy = b.y - a.y;
            float len_sq = dx * dx + dy * dy;
            if (len_sq > 1e-6f) {
                float t = ((mouse_world.x - a.x) * dx + (mouse_world.y - a.y) * dy) / len_sq;
                if (t < 0.f) t = 0.f;
                if (t > 1.f) t = 1.f;
                anchor = Pt(a.x + t * dx, a.y + t * dy);
            }
        }

        const Pt tip_screen = win.viewport.world_to_screen(anchor, cmin);
        ws.oscilloscope.set_hover_signal(signal_key);
        render_hover_scope_tooltip(doc, ws, signal_key, tip_screen);
        return;
    }
}

void CanvasRenderer::renderTempWire(BlueprintWindow& win, Pt cmin, ImDrawList* draw_list) {
    if (!win.input.has_temp_wire()) return;
    
    Pt start_world = win.input.temp_wire_start();
    Pt end_world = win.input.temp_wire_end_world();
    Pt s = win.viewport.world_to_screen(start_world, cmin);
    Pt e = win.viewport.world_to_screen(end_world, cmin);
    uint32_t color = win.input.is_reconnecting()
        ? CanvasColors::TEMP_WIRE_RECONNECT
        : CanvasColors::TEMP_WIRE_NEW;
    draw_list->AddLine(ImVec2(s.x, s.y), ImVec2(e.x, e.y), color, 2.0f);
}

void CanvasRenderer::renderMarquee(BlueprintWindow& win, Pt cmin, ImDrawList* draw_list) {
    if (!win.input.is_marquee_selecting()) return;
    
    auto dl = make_dl(draw_list);
    
    Pt ms = win.viewport.world_to_screen(win.input.marquee_start(), cmin);
    Pt me = win.viewport.world_to_screen(win.input.marquee_end(), cmin);
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
    mods.shift = io.KeyShift;

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
