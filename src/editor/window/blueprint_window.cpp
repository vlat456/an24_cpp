/// BlueprintWindow construction and factories.

#include "window/blueprint_window.h"

#include "core/model/component_registry.h"
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

ui::InternedId require_nested_id(ui::StringInterner& interner, const std::string& scope_id) {
    const ui::InternedId nested_id = interner.lookup(scope_id);
    if (nested_id.empty()) {
        throw std::logic_error("Embedded window construction failed: nested instance not found");
    }
    return nested_id;
}

std::unique_ptr<EditingHost> make_embedded_host(bp2::EditorModel& root_model,
                                                ui::StringInterner& interner,
                                                std::span<const std::string> scope_path) {
    std::vector<ui::InternedId> path;
    path.reserve(scope_path.size());
    for (const std::string& segment : scope_path) {
        path.push_back(require_nested_id(interner, segment));
    }
    return create_pathful_embedded_host(root_model, std::move(path));
}

void rebuild_root_scene(BlueprintWindow& window, const ComponentRegistry* parser_registry) {
    ComponentRegistry empty_reg;
    const ComponentRegistry& reg = parser_registry ? *parser_registry : empty_reg;
    visual::mutations::rebuild(window.scene, window.root_model.current(), window.interner,
        window.arena, std::span<const ui::InternedId>{}, reg, nullptr);
    window.input.rebuild_snapshot();
}

void rebuild_embedded_scene(BlueprintWindow& window, const ComponentRegistry* parser_registry) {
    ComponentRegistry empty_reg;
    const ComponentRegistry& reg = parser_registry ? *parser_registry : empty_reg;

    const bp2::Blueprint* embedded_bp =
        editor::resolve_embedded_blueprint(window.root_model.current(), window.interner, window.scope.path());
    if (!embedded_bp) {
        throw std::logic_error(
            "Embedded window construction failed: cannot resolve path '"
            + window.scope.sim_scope_prefix() + "'");
    }

    std::vector<ui::InternedId> instance_path;
    instance_path.reserve(window.scope.path().size());
    for (const std::string& segment : window.scope.path()) {
        instance_path.push_back(window.interner.lookup(segment));
    }
    visual::mutations::rebuild(window.scene, *embedded_bp, window.interner,
        window.arena, instance_path, reg, nullptr);
    window.input.rebuild_snapshot();
}

void rebuild_external_scene(BlueprintWindow& window, const ComponentRegistry* parser_registry) {
    ComponentRegistry empty_reg;
    const ComponentRegistry& reg = parser_registry ? *parser_registry : empty_reg;

    std::vector<ui::InternedId> instance_path;
    instance_path.reserve(window.scope.path().size());
    for (const std::string& segment : window.scope.path()) {
        const ui::InternedId scope_iid = window.interner.lookup(segment);
        if (scope_iid.empty()) {
            throw std::logic_error(
                "External window construction failed: unknown instance path '"
                + window.scope.sim_scope_prefix() + "'");
        }
        instance_path.push_back(scope_iid);
    }

    visual::mutations::rebuild(window.scene, require_external_blueprint(window.external_blueprint),
        window.rendered_interner(), window.rendered_arena(), instance_path, reg, nullptr);
    window.input.rebuild_snapshot();
}

} // namespace

/// A sentinel empty host for constructing CanvasInput before the real host
/// is available (external windows set host after blueprint is stored).
namespace {
class NullHost : public EditingHost {
public:
    const bp2::Blueprint bp_;  // owns empty blueprint for safe returns
    NullHost() = default;

    const bp2::Blueprint& current_blueprint() const override { return bp_; }
    const bp2::Blueprint::Node* find_node(ui::InternedId) const override { return nullptr; }
    const bp2::Blueprint::Wire* find_wire(ui::InternedId) const override { return nullptr; }
    const std::vector<bp2::Blueprint::Wire>& wires() const override { return bp_.wires(); }
    const std::vector<bp2::Blueprint::Node>& nodes() const override { return bp_.nodes(); }
    void push_checkpoint() override {}
    bool mutate_atomically(const std::function<void()>&) override { return false; }
    void replace_current(bp2::Blueprint) override {}
    bool add_wire(bp2::Blueprint::Wire) override { return false; }
    bool remove_wire(ui::InternedId) override { return false; }
    bool update_wire(ui::InternedId, std::function<void(bp2::Blueprint::Wire&)>) override { return false; }
    bool update_node_position(ui::InternedId, float, float) override { return false; }
    bool update_node(ui::InternedId, std::function<void(bp2::Blueprint::Node&)>) override { return false; }
    bool remove_node(ui::InternedId, std::vector<ui::InternedId>) override { return false; }
    bool bake_blueprint_instance(ui::InternedId, const bp2::BlueprintLibrary&) override { return false; }
    std::string allocate_wire_id() override { return {}; }
};
} // namespace

BlueprintWindow::BlueprintWindow(bp2::EditorModel& model,
                                  ui::StringInterner& interner,
                                  bp2::PathArena& arena,
                                  WindowScopeId scope,
                                  std::string title,
                                  std::unique_ptr<EditingHost> editing_host,
                                  bool read_only,
                                  const ComponentRegistry* parser_registry)
    : title(std::move(title))
    , scope(std::move(scope))
    , root_model(model)
    , interner(interner)
    , arena(arena)
    , scene()
    , viewport()
    , host(editing_host ? std::move(editing_host) : std::make_unique<NullHost>())
    , input(scene, viewport, *this->host, this->interner, this->arena, this->scope, parser_registry)
    , read_only(read_only)
    , type_registry(parser_registry)
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
        create_editor_model_host(ctx.model),
        false,
        ctx.type_registry));
    rebuild_root_scene(*window, ctx.type_registry);
    return window;
}

std::unique_ptr<BlueprintWindow> BlueprintWindow::create_embedded(
    const Context& ctx,
    WindowScopeId scope,
    std::string title) {
    auto host = make_embedded_host(ctx.model, ctx.interner, scope.path());
    auto window = std::unique_ptr<BlueprintWindow>(new BlueprintWindow(
        ctx.model,
        ctx.interner,
        ctx.arena,
        std::move(scope),
        std::move(title),
        std::move(host),
        false,
        ctx.type_registry));
    rebuild_embedded_scene(*window, ctx.type_registry);
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
        ctx.type_registry));

    window->external_blueprint = std::move(external_document.blueprint);
    window->external_interner = std::move(external_document.interner);
    window->external_arena = std::move(external_document.arena);

    // Now create the read-only host referencing the stored external blueprint.
    window->host = create_read_only_host(*window->external_blueprint);
    window->input.rebind_host(*window->host);
    window->input.rebind_identity_context(*window->external_interner, *window->external_arena);

    rebuild_external_scene(*window, ctx.type_registry);
    return window;
}

const bp2::Blueprint& BlueprintWindow::rendered_blueprint() const {
    if (scope.is_external()) {
        return require_external_blueprint(external_blueprint);
    }
    return require_host(host).current_blueprint();
}

ui::StringInterner& BlueprintWindow::rendered_interner() {
    if (scope.is_external() && external_interner) {
        return *external_interner;
    }
    return interner;
}

const ui::StringInterner& BlueprintWindow::rendered_interner() const {
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
