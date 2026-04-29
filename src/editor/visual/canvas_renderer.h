#pragma once

#include "editor/document.h"
#include "ui/math/pt.h"
#include "editor/window/blueprint_window.h"
#include "editor/window_system.h"
#include "editor/visual/canvas_constants.h"
#include "editor/visual/port/port_circle_atlas.h"

#ifdef AN24_PROFILE
#include "editor/app/frame_profiler.h"
#endif

struct ImDrawList;


class CanvasRenderer {
public:
    CanvasRenderer() {}

    void render(BlueprintWindow& win, Document& doc, WindowSystem& ws,
                Pt cmin, Pt cmax, ImDrawList* draw_list, bool hovered);

#ifdef AN24_PROFILE
    static an24::FrameProfiler& profiler();
#endif

private:
    void renderGrid(BlueprintWindow& win, Pt cmin, Pt cmax, ImDrawList* draw_list);
    void renderBlueprint(BlueprintWindow& win, Document& doc, WindowSystem& ws,
                         Pt cmin, Pt cmax, ImDrawList* draw_list);
    void renderTooltips(BlueprintWindow& win, Document& doc, WindowSystem& ws, Pt cmin, ImDrawList* draw_list);
    void renderTempWire(BlueprintWindow& win, Pt cmin, ImDrawList* draw_list);
    void renderMarquee(BlueprintWindow& win, Pt cmin, ImDrawList* draw_list);
    void handleInput(BlueprintWindow& win, Document& doc, WindowSystem& ws, Pt cmin);

    std::unordered_set<std::string_view, visual::StringViewHash> energized_buf_;

#ifdef AN24_EDITOR
    visual::PortCircleAtlas port_circle_atlas_;
#endif

#ifdef AN24_PROFILE
    int prof_grid_{};
    int prof_energized_{};
    int prof_crossings_{};
    int prof_scene_render_{};
    int prof_tooltips_{};
    int prof_input_{};
    bool profile_registered_{false};
#endif
};
