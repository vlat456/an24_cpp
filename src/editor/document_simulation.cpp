#include "document.h"
#include "simulation_bridge.h"

#include "core/solvers/jit/jit_build_input.h"
#include "visual/node/visual_node.h"
#include "visual/scene_mutations.h"
#include "identity.h"
#include <spdlog/spdlog.h>

namespace {

/// Dispatch a node color update to the window matching scope_id.
///
/// **Dual-path color contract (PUSH path):**
/// This function pushes the canonical `node.view.color` directly to the live
/// widget after a mutation, bypassing a full scene rebuild. The PULL path
/// (scene rebuild) reads the same `n.view.color` in `scene_mutations.cpp`.
/// Both paths must produce identical visual results via `NodeColor::to_uint32()`.
void dispatch_color_to_widget(WindowManager& window_manager,
                              core::StringInterner& interner,
                              core::InternedId node_iid,
                              const WindowScopeId& scope_id,
                              std::optional<editor::NodeColor> color) {
    BlueprintWindow* win = window_manager.find(scope_id);
    if (!win) return;
    std::string_view node_sv = interner.resolve(node_iid);
    if (auto* widget = win->scene.find(node_sv)) {
        widget->setCustomColor(color.has_value() ? std::optional<uint32_t>(color->to_uint32()) : std::nullopt);
    }
}

} // namespace

// ============================================================================
// Simulation lifecycle (thin delegations to SimulationBridge)
// ============================================================================

void Document::startSimulation() {
    sim_bridge_.start(sim_bridge_.build_jit_input(type_registry_, library_index_));
}

void Document::stopSimulation() {
    sim_bridge_.stop();
}

void Document::rebuildSimulation() {
    sim_bridge_.rebuild(sim_bridge_.build_jit_input(type_registry_, library_index_));
}

void Document::updateSimulationStep(double dt) {
    sim_bridge_.step(dt);
}

void Document::updateNodeContentFromSimulation() {
    sim_bridge_.update_node_content();
}

void Document::resetNodeContent(const ComponentRegistry& registry) {
    sim_bridge_.set_type_registry(&registry);
    sim_bridge_.reset_node_content();
}

void Document::purge_transient_node_state() {
    sim_bridge_.purge_transient_node_state();
}

// ============================================================================
// Window management (Document-level, uses bridge for runtime state)
// ============================================================================

void Document::rebuild_window_scenes() {
    ComponentRegistry empty_reg;
    const ComponentRegistry& reg = type_registry_ ? *type_registry_ : empty_reg;
    const auto& rt = sim_bridge_.runtime_node_states();

    for (auto& win : window_manager_.windows()) {
        std::vector<core::InternedId> instance_path = editor::scope_id_to_instance_path(win->resolved_scope_id());

        if (win->is_external_ref() && win->external_blueprint
            && win->external_interner && win->external_arena) {
            visual::mutations::rebuild(win->scene, *win->external_blueprint,
                                       *win->external_interner, *win->external_arena, instance_path, reg,
                                       &rt);
            win->input.rebuild_snapshot();
        } else if (win->resolved_scope_id().is_embedded()) {
            if (const bp2::Blueprint* embedded_bp = editor::resolve_embedded_blueprint(
                    model_.current(), win->resolved_scope_id().path())) {
                visual::mutations::rebuild(win->scene, *embedded_bp,
                                           interner_, arena_, instance_path, reg,
                                           &rt);
                win->input.rebuild_snapshot();
            } else {
                spdlog::error("[editor] Embedded window '{}' missing embedded blueprint during rebuild",
                              editor::instance_path_to_scope_string(interner_, win->resolved_scope_id().path()));
                continue;
            }
        } else {
            visual::mutations::rebuild(win->scene, model_.current(),
                                       interner_, arena_, instance_path, reg,
                                       &rt);
            win->input.rebuild_snapshot();
        }
    }
}

void Document::rebuildAllWindows() {
    for (auto& win : window_manager_.windows()) {
        win->input.cancel_gesture();
    }
    window_manager_.remove_orphaned_windows();
    rebuild_window_scenes();
    rebuildSimulation();
}

// ============================================================================
// Node color (Document-level — mutates blueprint model)
// ============================================================================

std::optional<editor::NodeColor> Document::node_color_for_scope(const WindowScopeId& scope_id,
                                                                  core::InternedId node_id) const {
    if (scope_id.is_external()) {
        return std::nullopt;
    }

    if (scope_id.is_root()) {
        const auto* node = model_.current().find_node(node_id);
        return node ? node->view.color : std::nullopt;
    }

    const bp2::Blueprint* embedded_bp = editor::resolve_embedded_blueprint(model_.current(), scope_id.path());
    if (!embedded_bp) {
        return std::nullopt;
    }
    const auto* node = embedded_bp->find_node(node_id);
    return node ? node->view.color : std::nullopt;
}

void Document::set_node_color_for_scope(const WindowScopeId& scope_id,
                                        core::InternedId node_id,
                                        std::optional<editor::NodeColor> color) {
    if (scope_id.is_external()) {
        return;
    }

    const std::optional<editor::NodeColor> canonical_color = color.has_value()
        ? std::optional<editor::NodeColor>(editor::NodeColor::canonicalized(*color))
        : std::nullopt;

    auto assign_color = [canonical_color](bp2::Blueprint::Node& node) {
        node.view.color = canonical_color;
    };

    bool resolved_target = false;

    if (scope_id.is_root()) {
        resolved_target = (model_.current().find_node(node_id) != nullptr);
        if (!resolved_target) {
            return;
        }
        model_.update_node(node_id, assign_color);
    } else {
        const bp2::Blueprint* embedded_bp = editor::resolve_embedded_blueprint(model_.current(), scope_id.path());
        resolved_target = embedded_bp && (embedded_bp->find_node(node_id) != nullptr);
        if (!resolved_target) {
            return;
        }

        model_.update_embedded_node(scope_id.path(), node_id, assign_color);
    }

    if (resolved_target) {
        dispatch_color_to_widget(window_manager_, interner_, node_id, scope_id, canonical_color);
    }
}
