#pragma once

#include "window/window_scope_id.h"
#include "visual/scene.h"
#include "visual/scene_mutations.h"
#include "viewport/viewport.h"
#include "input/canvas_input.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "blueprint_v2/path/path.h"
#include "ui/core/interned_id.h"
#include <cassert>
#include <string>
#include <optional>

struct TypeRegistry;

struct BlueprintWindow {
    std::string title;
    std::string scope_id;
    bool open = true;

    std::unique_ptr<bp2::EditorModel> embedded_model;
    bp2::EditorModel& model;
    ui::StringInterner& interner;
    bp2::PathArena& arena;
    visual::Scene scene;
    Viewport viewport;
    CanvasInput input;

    bool read_only = false;
    void set_read_only(bool v) { read_only = v; input.read_only = v; }

    /// Simulation-mode guard: blocks editing but allows widget interaction.
    void set_simulation_mode(bool v) { input.simulation_mode = v; }

    bool pending_auto_fit = false;

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

    static std::unique_ptr<bp2::EditorModel> make_embedded_model(
        bp2::EditorModel& root_model,
        ui::StringInterner& interner,
        const std::string& scope_id) {
        if (scope_id.empty()) {
            return nullptr;
        }
        const ui::InternedId group_iid = interner.lookup(scope_id);
        const bp2::Blueprint::Nested* nested = group_iid.empty()
            ? nullptr
            : root_model.current().find_nested(group_iid);
        if (!nested) {
            throw std::logic_error("Embedded window construction failed: nested instance not found");
        }
        if (!nested->is_embedded() || !nested->inline_def()) {
            throw std::logic_error("Embedded window construction failed: nested instance missing inline_def");
        }
        return std::make_unique<bp2::EditorModel>(*nested->inline_def());
    }

    BlueprintWindow(bp2::EditorModel& model_, ui::StringInterner& interner_,
                    bp2::PathArena& arena_,
                    const std::string& scope_id_,
                    const std::string& title_,
                    const TypeRegistry* parser_registry = nullptr)
        : title(title_)
        , scope_id(scope_id_)
        , embedded_model(make_embedded_model(model_, interner_, scope_id_))
        , model(embedded_model ? *embedded_model : model_)
        , interner(interner_)
        , arena(arena_)
        , scene()
        , viewport()
        , input(scene, viewport, model, interner_, arena_, "", parser_registry)
    {
        viewport.grid_step = model_.current().grid_step();

        if (embedded_model) {
            visual::mutations::rebuild(scene, embedded_model->current(), interner_, arena_, "");
            mode = BlueprintWindowMode::EmbeddedGroup;
        } else {
            visual::mutations::rebuild(scene, model_.current(), interner_, arena_, "");
        }
    }

    BlueprintWindow(const BlueprintWindow&) = delete;
    BlueprintWindow& operator=(const BlueprintWindow&) = delete;
    BlueprintWindow(BlueprintWindow&&) = delete;
    BlueprintWindow& operator=(BlueprintWindow&&) = delete;

    /// Check if this window is in external reference mode.
    bool is_external_ref() const { return mode == BlueprintWindowMode::ExternalReference; }

    /// Typed scope identity for this window.
    WindowScopeId resolved_scope_id() const {
        if (mode == BlueprintWindowMode::ExternalReference) {
            return WindowScopeId::external(parent_instance_id);
        }
        if (!scope_id.empty()) {
            return WindowScopeId::embedded(scope_id);
        }
        return WindowScopeId::root();
    }

    /// Get the blueprint to render (external or parent's filtered by layout_group).
    const bp2::Blueprint& rendered_blueprint() const {
        if (mode == BlueprintWindowMode::ExternalReference) {
            if (external_blueprint.has_value()) {
                return *external_blueprint;
            }
            assert(false && "ExternalReference window missing external_blueprint");
            throw std::logic_error("ExternalReference window missing external_blueprint");
        }
        if (mode == BlueprintWindowMode::EmbeddedGroup) {
            assert(embedded_model && "EmbeddedGroup window must have embedded_model");
            if (!embedded_model) {
                throw std::logic_error("EmbeddedGroup window missing embedded_model");
            }
            return embedded_model->current();
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
