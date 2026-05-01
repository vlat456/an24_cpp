#include "canvas_renderer.h"
#include "editor/visual/renderer/grid_renderer.h"
#include "editor/visual/render_context.h"
#include "editor/visual/presentation/canvas_scene_snapshot.h"
#include "editor/visual/wire/wire.h"
#include "editor/visual/port/visual_port.h"
#include "editor/visual/renderer/draw_list.h"
#include "editor/imgui_draw_list.h"
#include "editor/visual/oscilloscope_plot.h"
#include "editor/input/input_types.h"
#include "editor/input/key_handler.h"
#include "input/canvas_input.h"
#include "blueprint_v2/path/path.h"
#include <imgui.h>
#include <algorithm>
#include <unordered_set>
#include <cstdio>
#include <cstdlib>

an24::FrameProfiler& CanvasRenderer::profiler() {
    static an24::FrameProfiler p;
    static bool init = false;
    if (!init) {
        init = true;
        (void)p.register_section("canvas: grid");
        (void)p.register_section("canvas: energized_set");
        (void)p.register_section("canvas: wire_crossings");
        (void)p.register_section("canvas: scene.render");
        (void)p.register_section("canvas: tooltips");
        (void)p.register_section("canvas: input");
    }
    return p;
}

// ===========================================================================
// Development diagnostics: hover signal key resolution trace
// ===========================================================================

static bool maybe_log_hover_signal_resolution(
    const std::string& visual_node,
    const std::string& visual_port,
    core::InternedId resolved_key,
    float value) {
    static bool initialized = false;
    static bool enabled = false;

    if (!initialized) {
        const char* env = std::getenv("AN24_EDITOR_DEBUG_SIGNAL_KEYS");
        enabled = (env != nullptr && env[0] == '1');
        initialized = true;
    }

    if (enabled) {
        std::string_view key_sv = resolved_key.empty() ? std::string_view{} : std::string_view{"(resolved)"};
        std::fprintf(stdout, "[DBG-HOVER] %s.%s => %.*s (%.2fV)\n",
                visual_node.c_str(), visual_port.c_str(),
                static_cast<int>(key_sv.size()), key_sv.data(), value);
        std::fflush(stdout);
    }

    return enabled;
}

static void render_probe_markers(BlueprintWindow& win, Document& doc, WindowSystem& ws,
                                 Pt cmin, ImDrawList* draw_list) {
    if (!ws.showOscilloscope) return;
    ws.oscilloscope.for_each_probe_in_scope(doc.id(), win.resolved_scope_id(),
        [&](const OscilloscopeProbe& probe) {
            Pt sp = win.viewport.world_to_screen(probe.world_pos, cmin);
            visual::osc::draw_probe_marker(draw_list, sp, probe.color);
        });
}

static void render_hover_scope_tooltip(Document& doc,
                                       WindowSystem& ws,
                                       core::InternedId signal_iid,
                                       const Pt& anchor_screen) {
    const std::deque<float>& samples = ws.oscilloscope.hover_samples(doc.id());
    if (samples.empty()) return;

    // Resolve label from signal InternedId for display.
    std::string label = "<signal>";
    if (!signal_iid.empty()) {
        std::string_view sv = doc.signal_key_interner().resolve(signal_iid);
        label = std::string{sv};
    }

    OscilloscopeProbe pseudo;
    pseudo.label = label;
    pseudo.wire_iid = core::InternedId{0};  // synthetic — no real wire
    pseudo.color = IM_COL32(80, 200, 255, 255);
    pseudo.samples = samples;  // Copy hover samples into pseudo for rendering.

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
        one.push_back({&pseudo});
        float min_v = 0.0f;
        float max_v = 0.0f;
        visual::osc::compute_range(one, min_v, max_v);
        visual::osc::render_channel_plot(pseudo, pseudo.samples, min_v, max_v, 72.0f, -1.0f);
        visual::osc::render_stats_row(OscilloscopeModel::compute_stats(pseudo.samples, ws.oscilloscope.sample_period_sec()));
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
    auto& prof = profiler();

    if (hovered) {
        ImVec2 mp = ImGui::GetMousePos();
        Pt mouse_world = win.viewport.screen_to_world(Pt(mp.x, mp.y), cmin);
        win.input.update_hover(mouse_world);
    } else {
        win.input.update_hover(Pt(CanvasConstants::HOVER_CLEAR_X, CanvasConstants::HOVER_CLEAR_Y));
    }

    { SCOPED_PROFILE(prof, prof_grid_);
    renderGrid(win, cmin, cmax, draw_list);
    }

    renderBlueprint(win, doc, ws, cmin, cmax, draw_list);

    { SCOPED_PROFILE(prof, prof_tooltips_);
    renderTooltips(win, doc, ws, cmin, draw_list);
    renderTempWire(win, cmin, draw_list);
    render_probe_markers(win, doc, ws, cmin, draw_list);
    renderMarquee(win, cmin, draw_list);
    }

    if (hovered) {
        SCOPED_PROFILE(prof, prof_input_);
        handleInput(win, doc, ws, cmin);
    }

    prof.add_frame(0.0);
    // prof.maybe_report() intentionally omitted — canvas render profiling data
    // is collected but not printed to avoid console spam.
}

void CanvasRenderer::renderGrid(BlueprintWindow& win, Pt cmin, Pt cmax, ImDrawList* draw_list) {
    auto dl = make_dl(draw_list);
    visual::GridRenderer grid;
    grid.render(dl, win.viewport, cmin, cmax);
}

void CanvasRenderer::renderBlueprint(BlueprintWindow& win, Document& doc, WindowSystem& ws,
                                      Pt cmin, Pt cmax, ImDrawList* draw_list) {
    auto dl = make_dl(draw_list);
    auto& prof = profiler();

    { SCOPED_PROFILE(prof, prof_energized_);
    energized_buf_.clear();
    doc.buildEnergizedWireSet(energized_buf_, win.resolved_scope_id());
    }

    auto sel_nodes = win.input.selected_node_id_views();

    if (win.scene.crossings_dirty()) {
        SCOPED_PROFILE(prof, prof_crossings_);
        visual::compute_wire_crossings(win.scene);
        win.scene.clear_crossings_dirty();
    }

    SCOPED_PROFILE(prof, prof_scene_render_);

    visual::RenderContext ctx;
    ctx.zoom = win.viewport.zoom;
    ctx.pan = win.viewport.pan;
    ctx.canvas_min = cmin;
    ctx.selected_node_ids = &sel_nodes;
    ctx.selected_wire_id = win.input.selected_wire_id();
    ctx.hovered_wire_id = win.input.hovered_wire_id();
    ctx.hovered_routing_point = win.input.hovered_routing_point_id();
    ctx.energized_wires = energized_buf_.empty() ? nullptr : &energized_buf_;
    ctx.show_debug_bounds = ws.showDebugLayoutBounds;
    ctx.show_debug_paint_bounds = ws.showDebugPaintBounds;

    // Build cache bypass predicate from transient input state.
    // Each InputState that modifies node geometry in-place during drag
    // maps to a bypass so the cache is skipped for the affected node.
    if (win.input.state() == InputState::ResizingNode) {
        std::string_view resize_id = win.input.resize_node_id();
        ctx.cache_bypass = [resize_id](std::string_view id) {
            return id == resize_id;
        };
    }

    port_circle_atlas_.ensure();
    ctx.port_circle_texture = port_circle_atlas_.texture_id();

    // Per-window sprite cache — each window scope gets its own GL textures.
    auto& cache_ptr = window_caches_[win.scope];
    if (!cache_ptr) cache_ptr = std::make_unique<visual::NodeSpriteCache>();
    cache_ptr->bake_dirty_nodes(win.scene, ctx);
    ctx.sprite_cache = cache_ptr.get();

    win.scene.render(&dl, ctx);
}

void CanvasRenderer::renderTooltips(BlueprintWindow& win, Document& doc, WindowSystem& ws,
                                    Pt cmin, ImDrawList* draw_list) {
    (void)draw_list;
    if (!doc.isSimulationRunning()) return;

    ImVec2 mp = ImGui::GetMousePos();
    Pt mouse_screen(mp.x, mp.y);
    Pt mouse_world = win.viewport.screen_to_world(mouse_screen, cmin);

    core::StringInterner& rendered_interner = win.rendered_interner();
    const auto snapshot = editor::presentation::build_canvas_scene_snapshot(win.scene, rendered_interner);
    auto hit = editor::presentation::hit_test_canvas_scene(snapshot, mouse_world);
    ws.oscilloscope.clear_hover_signal(doc.id());

    if (auto* hp = std::get_if<visual::HitPort>(&hit)) {
        std::string_view node_id = rendered_interner.resolve(hp->node_id);
        if (node_id.empty()) return;

        std::string_view port_name = rendered_interner.resolve(hp->port_name);
        Pt port_screen = win.viewport.world_to_screen(hp->center - Pt(visual::PortConstants::RADIUS, visual::PortConstants::RADIUS), cmin);
        core::InternedId signal_iid = doc.resolve_endpoint_signal_key(
            win.resolved_scope_id(), node_id, port_name);
        if (signal_iid.empty()) return;

        // Dev-only diagnostics: log signal resolution on hover (if AN24_EDITOR_DEBUG_SIGNAL_KEYS=1)
        float current_value = doc.get_signal_value(signal_iid);
        maybe_log_hover_signal_resolution(std::string(node_id), std::string(port_name), signal_iid, current_value);

        ws.oscilloscope.set_hover_signal(doc.id(), signal_iid);
        render_hover_scope_tooltip(doc, ws, ws.oscilloscope.hover_signal_key(doc.id()), port_screen);
        return;

    } else if (auto* hw = std::get_if<visual::HitWire>(&hit)) {
        std::string_view wire_id = rendered_interner.resolve(hw->wire_id);
        core::InternedId signal_iid = doc.resolve_wire_signal_key(
            win.resolved_scope_id(), wire_id);
        if (signal_iid.empty()) return;

        // Dev-only diagnostics: log signal resolution on hover (if AN24_EDITOR_DEBUG_SIGNAL_KEYS=1)
        float current_value = doc.get_signal_value(signal_iid);
        maybe_log_hover_signal_resolution(std::string(wire_id), "src", signal_iid, current_value);
           
        // Project mouse onto wire segment for tooltip anchor
        auto* found = win.scene.find(wire_id);
        auto* wire = (found && found->kind() == ui::WidgetKind::Wire)
                     ? static_cast<visual::Wire*>(found) : nullptr;
        if (!wire) return;
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
         ws.oscilloscope.set_hover_signal(doc.id(), signal_iid);
         render_hover_scope_tooltip(doc, ws, ws.oscilloscope.hover_signal_key(doc.id()), tip_screen);
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
        auto action = doc.applyInputResult(result, win.resolved_scope_id());
        ws.handleInputAction(action, doc);
    };

    if (io.MouseWheel != 0.0f) {
        dispatch(win.input.on_scroll(io.MouseWheel * CanvasConstants::SCROLL_ZOOM_FACTOR, screen_pos, cmin));
    }

    bool was_dbl_consumed = false;
    if (ImGui::IsMouseDoubleClicked(0)) {
        InputResult dbl_result = win.input.on_double_click(screen_pos, cmin);
        was_dbl_consumed = dbl_result.double_click_consumed;
        dispatch(std::move(dbl_result));
    }

    if (!was_dbl_consumed && ImGui::IsMouseClicked(0)) {
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

void CanvasRenderer::evict_window(const WindowScopeId& scope) {
    window_caches_.erase(scope);
}

void CanvasRenderer::gc_stale_caches(const std::vector<std::unique_ptr<BlueprintWindow>>& live_windows) {
    auto it = window_caches_.begin();
    while (it != window_caches_.end()) {
        const WindowScopeId& scope = it->first;
        bool found = false;
        for (const auto& win : live_windows) {
            if (win->resolved_scope_id() == scope) { found = true; break; }
        }
        if (!found) {
            it = window_caches_.erase(it);
        } else {
            ++it;
        }
    }
}
