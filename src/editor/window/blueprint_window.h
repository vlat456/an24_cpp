#pragma once

#include "input/canvas_input.h"
#include "input/editing_host.h"
#include "viewport/viewport.h"
#include "visual/scene.h"
#include "visual/scene_mutations.h"
#include "window/window_scope_id.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "blueprint_v2/path/path.h"

#include <optional>
#include <string>

struct TypeRegistry;

struct RootWindowTag {};
struct EmbeddedWindowTag {};

struct BlueprintWindow {
    static const bp2::Blueprint& require_external_blueprint(const std::optional<bp2::Blueprint>& bp) {
        if (!bp.has_value()) {
            throw std::logic_error("ExternalReference window missing external_blueprint");
        }
        return *bp;
    }

    static const EditingHost& require_host(const std::unique_ptr<EditingHost>& host) {
        if (!host) {
            throw std::logic_error("BlueprintWindow missing editing host");
        }
        return *host;
    }

    static ui::InternedId require_nested_id(ui::StringInterner& interner, const std::string& scope_id) {
        const ui::InternedId nested_id = interner.lookup(scope_id);
        if (nested_id.empty()) {
            throw std::logic_error("Embedded window construction failed: nested instance not found");
        }
        return nested_id;
    }

    static const bp2::Blueprint::Nested& require_embedded_nested(bp2::EditorModel& root_model,
                                                                 ui::InternedId nested_id) {
        const auto* nested = root_model.current().find_nested(nested_id);
        if (!nested) {
            throw std::logic_error("Embedded window construction failed: nested instance not found");
        }
        if (!nested->is_embedded() || !nested->inline_def()) {
            throw std::logic_error("Embedded window construction failed: nested instance missing inline_def");
        }
        return *nested;
    }

    static std::unique_ptr<EditingHost> make_embedded_host(bp2::EditorModel& root_model,
                                                           ui::StringInterner& interner,
                                                           const std::string& embedded_scope_id) {
        return create_embedded_inline_host(root_model,
                                           require_nested_id(interner, embedded_scope_id));
    }

    std::string title;
    WindowScopeId scope;
    bool open = true;

    bp2::EditorModel& root_model;
    ui::StringInterner& interner;
    bp2::PathArena& arena;
    visual::Scene scene;
    Viewport viewport;
    std::unique_ptr<EditingHost> host;
    CanvasInput input;

    bool read_only = false;
    void set_read_only(bool v) { read_only = v; input.read_only = v; }

    void set_simulation_mode(bool v) { input.simulation_mode = v; }

    bool pending_auto_fit = false;

    BlueprintWindowMode mode = BlueprintWindowMode::RootDocument;

    std::optional<bp2::Blueprint> external_blueprint;
    std::unique_ptr<ui::StringInterner> external_interner;
    std::unique_ptr<bp2::PathArena> external_arena;

    void set_external_identity(const std::string& instance_id) {
        scope = WindowScopeId::external(instance_id);
        mode = BlueprintWindowMode::ExternalReference;
    }

    BlueprintWindow(RootWindowTag,
                    bp2::EditorModel& model_,
                    ui::StringInterner& interner_,
                    bp2::PathArena& arena_,
                    const std::string& title_,
                    const TypeRegistry* parser_registry = nullptr)
        : title(title_)
        , scope(WindowScopeId::root())
        , root_model(model_)
        , interner(interner_)
        , arena(arena_)
        , scene()
        , viewport()
        , host(create_editor_model_host(root_model))
        , input(scene, viewport, *host, interner_, arena_, "", parser_registry) {
        viewport.grid_step = root_model.current().grid_step();
        visual::mutations::rebuild(scene, root_model.current(), interner_, arena_, "");
    }

    BlueprintWindow(EmbeddedWindowTag,
                    bp2::EditorModel& model_,
                    ui::StringInterner& interner_,
                    bp2::PathArena& arena_,
                    const std::string& embedded_scope_id,
                    const std::string& title_,
                    const TypeRegistry* parser_registry = nullptr)
        : title(title_)
        , scope(WindowScopeId::embedded(embedded_scope_id))
        , root_model(model_)
        , interner(interner_)
        , arena(arena_)
        , scene()
        , viewport()
        , host(make_embedded_host(root_model, interner_, embedded_scope_id))
        , input(scene, viewport, *host, interner_, arena_, "", parser_registry)
        , mode(BlueprintWindowMode::EmbeddedGroup) {
        const auto& nested = require_embedded_nested(root_model, require_nested_id(interner_, embedded_scope_id));
        viewport.grid_step = nested.inline_def()->grid_step();
        visual::mutations::rebuild(scene, *nested.inline_def(), interner_, arena_, "");
    }

    BlueprintWindow(const BlueprintWindow&) = delete;
    BlueprintWindow& operator=(const BlueprintWindow&) = delete;
    BlueprintWindow(BlueprintWindow&&) = delete;
    BlueprintWindow& operator=(BlueprintWindow&&) = delete;

    bool is_external_ref() const { return mode == BlueprintWindowMode::ExternalReference; }

    /// For external use only: returns scope key as string.
    /// Internal WindowManager comparisons use typed WindowScopeId.
    std::string scope_id_str() const {
        return scope.key();
    }

    WindowScopeId resolved_scope_id() const {
        return scope;
    }

    const bp2::Blueprint& rendered_blueprint() const {
        if (mode == BlueprintWindowMode::ExternalReference) {
            return require_external_blueprint(external_blueprint);
        }
        return require_host(host).current_blueprint();
    }

    ui::StringInterner& rendered_interner() {
        if (mode == BlueprintWindowMode::ExternalReference && external_interner) {
            return *external_interner;
        }
        return interner;
    }

    bp2::PathArena& rendered_arena() {
        if (mode == BlueprintWindowMode::ExternalReference && external_arena) {
            return *external_arena;
        }
        return arena;
    }
};
