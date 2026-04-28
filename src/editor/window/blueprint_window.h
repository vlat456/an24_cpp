#pragma once

#include "window/window_scope_id.h"
#include "editor/input/canvas_input.h"
#include "editor/visual/presentation/node_badge.h"
#include "editor/visual/render_context.h"
#include "editor/visual/scene.h"
#include "editor/input/editing_host.h"
#include "editor/viewport/viewport.h"
#include "core/strings/interned_id.h"
#include <memory>
#include <optional>
#include <string>

struct ComponentRegistry;

/// A window that renders a blueprint canvas (root, embedded, or external-ref).
struct BlueprintWindow {
    struct Context {
        bp2::EditorModel& model;
        core::StringInterner& interner;
        bp2::PathArena& arena;
        const ComponentRegistry* type_registry = nullptr;
        const editor::IconFont* icon_font = nullptr;
    };

    struct ExternalDocument {
        bp2::Blueprint blueprint;
        std::unique_ptr<core::StringInterner> interner;
        std::unique_ptr<bp2::PathArena> arena;
    };

    static std::unique_ptr<BlueprintWindow> create_root(
        const Context& ctx,
        std::string title = "Root");

    static std::unique_ptr<BlueprintWindow> create_embedded(
        const Context& ctx,
        WindowScopeId scope,
        std::string title);

    static std::unique_ptr<BlueprintWindow> create_external(
        const Context& ctx,
        WindowScopeId scope,
        std::string title,
        ExternalDocument external_document);

    BlueprintWindow(const BlueprintWindow&) = delete;
    BlueprintWindow& operator=(const BlueprintWindow&) = delete;
    BlueprintWindow(BlueprintWindow&&) = delete;
    BlueprintWindow& operator=(BlueprintWindow&&) = delete;

    bool is_external_ref() const { return scope.is_external(); }
    const WindowScopeId& resolved_scope_id() const { return scope; }

    const bp2::Blueprint& rendered_blueprint() const;
    core::StringInterner& rendered_interner();
    const core::StringInterner& rendered_interner() const;
    bp2::PathArena& rendered_arena();
    const bp2::PathArena& rendered_arena() const;

    std::string title;
    WindowScopeId scope;
    bool open = true;

    bp2::EditorModel& root_model;
    core::StringInterner& interner;
    bp2::PathArena& arena;
    visual::Scene scene;
    Viewport viewport;
    std::unique_ptr<EditingHost> host;
    CanvasInput input;

    bool read_only = false;
    void set_read_only(bool v) { read_only = v; input.read_only = v; }
    void set_simulation_mode(bool v) { input.simulation_mode = v; }

    bool pending_auto_fit = false;

    std::optional<bp2::Blueprint> external_blueprint;
    std::unique_ptr<core::StringInterner> external_interner;
    std::unique_ptr<bp2::PathArena> external_arena;

private:
    BlueprintWindow(bp2::EditorModel& model,
                    core::StringInterner& interner,
                    bp2::PathArena& arena,
                    WindowScopeId scope,
                    std::string title,
                    std::unique_ptr<EditingHost> host,
                    bool read_only,
                    const editor::IconFont* icon_font);
};
