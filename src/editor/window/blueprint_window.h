#pragma once

#include "visual/scene.h"
#include "visual/scene_mutations.h"
#include "viewport/viewport.h"
#include "input/canvas_input.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "blueprint_v2/path/path.h"
#include "ui/core/interned_id.h"
#include <string>

struct BlueprintWindow {
    std::string title;
    std::string group_id;
    bool open = true;

    bp2::EditorModel& model;
    ui::StringInterner& interner;
    bp2::PathArena& arena;
    visual::Scene scene;
    Viewport viewport;
    CanvasInput input;

    bool read_only = false;
    void set_read_only(bool v) { read_only = v; input.read_only = v; }

    BlueprintWindow(bp2::EditorModel& model_, ui::StringInterner& interner_,
                    bp2::PathArena& arena_,
                    const std::string& group_id_,
                    const std::string& title_)
        : title(title_)
        , group_id(group_id_)
        , model(model_)
        , interner(interner_)
        , arena(arena_)
        , scene()
        , viewport()
        , input(scene, viewport, model_, interner_, arena_, group_id)
    {
        viewport.grid_step = model_.current().grid_step();
        visual::mutations::rebuild(scene, model_.current(), interner_, arena_, group_id_);
    }

    BlueprintWindow(const BlueprintWindow&) = delete;
    BlueprintWindow& operator=(const BlueprintWindow&) = delete;
    BlueprintWindow(BlueprintWindow&&) = delete;
    BlueprintWindow& operator=(BlueprintWindow&&) = delete;
};
