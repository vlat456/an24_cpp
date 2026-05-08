/// BlueprintWindow construction and factories.

#include "window/blueprint_window.h"

#include "core/model/component_registry.h"
#include "document_simulation_internal.h"
#include "embedded_path_utils.h"
#include "visual/scene_mutations.h"

#include <stdexcept>

namespace {

const bp2::Blueprint& require_external_blueprint(const std::optional<bp2::Blueprint>& bp) {
    if (!bp.has_value()) {
        throw std::logic_error("ExternalReference window missing external_blueprint");
    }
    return *bp;
}

const EditingHost& require_host(const std::unique_ptr<EditingHost>& host) {
    if (!host) {
        throw std::logic_error("BlueprintWindow missing editing host");
    }
    return *host;
}

// No conversion needed - scope_id.path() already returns InternedId
std::unique_ptr<EditingHost> make_embedded_host(bp2::EditorModel& root_model,
                                                core::StringInterner& /*interner*/,
                                                std::span<const core::InternedId> scope_path,
                                                const ComponentRegistry* registry,
                                                core::StringInterner* interner_ptr,
                                                const bp2::PathArena* arena) {
    return create_pathful_embedded_host(root_model,
        std::vector<core::InternedId>(scope_path.begin(), scope_path.end()),
        registry, interner_ptr, arena);
}

void rebuild_root_scene(BlueprintWindow& window, const ComponentRegistry* parser_registry,
                            const editor::IconFont* icon_font) {
    ComponentRegistry const empty_reg;
    const ComponentRegistry& reg = parser_registry ? *parser_registry : empty_reg;
    visual::mutations::rebuild(window.scene, window.root_model.current(), window.interner,
        window.arena, std::span<const core::InternedId>{}, reg, nullptr, icon_font);
    window.input.rebuild_snapshot();
}

void rebuild_embedded_scene(BlueprintWindow& window, const ComponentRegistry* parser_registry,
                                const editor::IconFont* icon_font) {
    ComponentRegistry const empty_reg;
    const ComponentRegistry& reg = parser_registry ? *parser_registry : empty_reg;

    const bp2::Blueprint* embedded_bp =
        editor::resolve_embedded_blueprint(window.root_model.current(), window.scope.path());
    if (!embedded_bp) {
        throw std::logic_error(
            "Embedded window construction failed: cannot resolve path '"
            + editor::instance_path_to_scope_string(window.interner, window.scope.path()) + "'");
    }

    std::vector<core::InternedId> instance_path(window.scope.path().begin(), window.scope.path().end());
    visual::mutations::rebuild(window.scene, *embedded_bp, window.interner,
        window.arena, instance_path, reg, nullptr, icon_font);
    window.input.rebuild_snapshot();
}

void rebuild_external_scene(BlueprintWindow& window, const ComponentRegistry* parser_registry,
                                const editor::IconFont* icon_font) {
    ComponentRegistry const empty_reg;
    const ComponentRegistry& reg = parser_registry ? *parser_registry : empty_reg;

    std::vector<core::InternedId> instance_path(window.scope.path().begin(), window.scope.path().end());

    visual::mutations::rebuild(window.scene, require_external_blueprint(window.external_blueprint),
        window.rendered_interner(), window.rendered_arena(), instance_path, reg, nullptr, icon_font);
    window.input.rebuild_snapshot();
}

} // namespace

BlueprintWindow::BlueprintWindow(bp2::EditorModel& model,
                                  core::StringInterner& interner,
                                  bp2::PathArena& arena,
                                  WindowScopeId scope,
                                  std::string title,
                                  std::unique_ptr<EditingHost> editing_host,
                                  bool read_only,
                                  const editor::IconFont* icon_font)
    : title(std::move(title))
    , scope(std::move(scope))
    , root_model(model)
    , interner(interner)
    , arena(arena)
    , scene()
    , viewport()
    , host(std::move(editing_host))  // may be nullptr for external windows
    , input(scene, viewport, this->host.get(), this->interner, this->arena, this->scope, icon_font)
    , read_only(read_only)
{
    input.read_only = read_only;
}

std::unique_ptr<BlueprintWindow> BlueprintWindow::create_root(
    const Context& ctx,
    std::string title) {
    auto window = std::unique_ptr<BlueprintWindow>(new BlueprintWindow(
        ctx.model,
        ctx.interner,
        ctx.arena,
        WindowScopeId::root(),
        std::move(title),
        create_editor_model_host(ctx.model, ctx.type_registry, &ctx.interner, &ctx.arena),
        false,
        ctx.icon_font));
    rebuild_root_scene(*window, ctx.type_registry, ctx.icon_font);
    return window;
}

std::unique_ptr<BlueprintWindow> BlueprintWindow::create_embedded(
    const Context& ctx,
    WindowScopeId scope,
    std::string title) {
    auto host = make_embedded_host(ctx.model, ctx.interner, scope.path(),
        ctx.type_registry, &ctx.interner, &ctx.arena);
    auto window = std::unique_ptr<BlueprintWindow>(new BlueprintWindow(
        ctx.model,
        ctx.interner,
        ctx.arena,
        std::move(scope),
        std::move(title),
        std::move(host),
        false,
        ctx.icon_font));
    rebuild_embedded_scene(*window, ctx.type_registry, ctx.icon_font);
    return window;
}

std::unique_ptr<BlueprintWindow> BlueprintWindow::create_external(
    const Context& ctx,
    WindowScopeId scope,
    std::string title,
    ExternalDocument external_document) {
    // External windows use a read-only host bound to the external blueprint,
    // not the root model. This eliminates the split-brain where host resolves
    // against root but rendered blueprint is external.
    //
    // The blueprint must be stored before the host is created, because the
    // host holds a const reference to it.
    auto window = std::unique_ptr<BlueprintWindow>(new BlueprintWindow(
        ctx.model,
        ctx.interner,
        ctx.arena,
        std::move(scope),
        std::move(title),
        nullptr,  // host created after blueprint is stored
        true,
        ctx.icon_font));

    window->external_blueprint = std::move(external_document.blueprint);
    window->external_interner = std::move(external_document.interner);
    window->external_arena = std::move(external_document.arena);

    // Now create the read-only host referencing the stored external blueprint.
    window->host = create_read_only_host(*window->external_blueprint);
    window->input.rebind_host(*window->host);
    window->input.rebind_identity_context(*window->external_interner, *window->external_arena);

    rebuild_external_scene(*window, ctx.type_registry, ctx.icon_font);
    return window;
}

const bp2::Blueprint& BlueprintWindow::rendered_blueprint() const {
    if (scope.is_external()) {
        return require_external_blueprint(external_blueprint);
    }
    return require_host(host).current_blueprint();
}

core::StringInterner& BlueprintWindow::rendered_interner() {
    if (scope.is_external() && external_interner) {
        return *external_interner;
    }
    return interner;
}

const core::StringInterner& BlueprintWindow::rendered_interner() const {
    if (scope.is_external() && external_interner) {
        return *external_interner;
    }
    return interner;
}

bp2::PathArena& BlueprintWindow::rendered_arena() {
    if (scope.is_external() && external_arena) {
        return *external_arena;
    }
    return arena;
}

const bp2::PathArena& BlueprintWindow::rendered_arena() const {
    if (scope.is_external() && external_arena) {
        return *external_arena;
    }
    return arena;
}
