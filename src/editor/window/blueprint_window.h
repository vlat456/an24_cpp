#pragma once

#include "visual/scene.h"
#include "visual/scene_mutations.h"
#include "viewport/viewport.h"
#include "input/canvas_input.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "blueprint_v2/path/path.h"
#include "ui/core/interned_id.h"
#include <string>
#include <optional>

/// Rendering mode for a BlueprintWindow.
enum class BlueprintWindowMode {
    RootDocument,       ///< Main document canvas (group_id is empty)
    EmbeddedGroup,      ///< Embedded sub-blueprint filtered by group_id
    ExternalReference,  ///< Read-only view of external blueprint, signals mapped through parent
};

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

    // ── External reference mode ──

    BlueprintWindowMode mode = BlueprintWindowMode::RootDocument;

    /// For ExternalReference mode: the loaded external blueprint snapshot.
    /// Scene is rebuilt from this blueprint (root scope) instead of the parent's.
    std::optional<bp2::Blueprint> external_blueprint;

    /// For ExternalReference mode: dedicated interner/arena for the external blueprint.
    /// These own the string storage for paths/IDs within the external blueprint.
    std::unique_ptr<ui::StringInterner> external_interner;
    std::unique_ptr<bp2::PathArena> external_arena;

    /// For ExternalReference mode: the parent instance id (e.g. "firstorderlag_1").
    /// Signal keys are mapped: child "node.port" → parent "parent_instance_id:node.port".
    std::string parent_instance_id;

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
        if (!group_id_.empty()) {
            mode = BlueprintWindowMode::EmbeddedGroup;
        }
    }

    BlueprintWindow(const BlueprintWindow&) = delete;
    BlueprintWindow& operator=(const BlueprintWindow&) = delete;
    BlueprintWindow(BlueprintWindow&&) = delete;
    BlueprintWindow& operator=(BlueprintWindow&&) = delete;

    /// Check if this window is in external reference mode.
    bool is_external_ref() const { return mode == BlueprintWindowMode::ExternalReference; }

    /// Get the blueprint to render (external or parent's filtered by group_id).
    const bp2::Blueprint& rendered_blueprint() const {
        if (mode == BlueprintWindowMode::ExternalReference && external_blueprint.has_value()) {
            return *external_blueprint;
        }
        return model.current();
    }

    /// Get the interner for the rendered blueprint.
    ui::StringInterner& rendered_interner() {
        if (mode == BlueprintWindowMode::ExternalReference && external_interner) {
            return *external_interner;
        }
        return interner;
    }

    /// Get the arena for the rendered blueprint.
    bp2::PathArena& rendered_arena() {
        if (mode == BlueprintWindowMode::ExternalReference && external_arena) {
            return *external_arena;
        }
        return arena;
    }
};
