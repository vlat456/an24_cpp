#include "document.h"
#include "identity.h"

Document::InputResultAction Document::applyInputResult(const InputResult& r,
                                                        const std::string& scope_id) {
    const WindowScopeId typed_scope = scope_id.empty()
        ? WindowScopeId::root()
        : WindowScopeId::embedded(scope_id);
    return applyInputResult(r, typed_scope);
}

Document::InputResultAction Document::applyInputResult(const InputResult& r,
                                                        const WindowScopeId& scope_id) {
    InputResultAction action;
    const std::string legacy_scope_id = scope_id.is_embedded() ? scope_id.key() : "";

    if (scope_id.is_embedded()) {
        BlueprintWindow* win = window_manager_.find(scope_id.key());
        if (win && win->embedded_model) {
            const ui::InternedId nested_iid = interner_.lookup(scope_id.key());
            const bp2::Blueprint::Nested* nested = nested_iid.empty()
                ? nullptr
                : model_.current().find_nested(nested_iid);
            if (nested && nested->inline_def()
                && *nested->inline_def() != win->embedded_model->current()) {
                bp2::Blueprint::Nested updated = *nested;
                updated.set_inline_def(std::make_unique<bp2::Blueprint>(win->embedded_model->current()));
                model_.push_checkpoint();
                model_.replace_current(bp2::replace_nested_preserve_order(model_.current(), std::move(updated)));
            }
        }
    }

    if (r.rebuild_simulation) {
        rebuildSimulation();
        window_manager_.remove_orphaned_windows();
    }
    if (r.show_context_menu) {
        action.show_context_menu = true;
        action.context_menu_pos = r.context_menu_pos;
        action.context_menu_scope_id = legacy_scope_id;
    }
    if (r.show_node_context_menu) {
        action.show_node_context_menu = true;
        action.context_menu_node_id = editor::NodeId::from_string(r.context_menu_node_id);
        action.node_context_menu_scope_id = legacy_scope_id;
    }
    if (!r.open_sub_window.empty()) {
        openSubWindow(r.open_sub_window);
    }
    if (!r.toggle_switch_node_id.empty()) {
        triggerSwitch(editor::NodeId::from_string(r.toggle_switch_node_id), legacy_scope_id);
    }
    if (!r.slider_node_id.empty()) {
        setSliderValue(editor::NodeId::from_string(r.slider_node_id), r.slider_value, legacy_scope_id);
    }
    if (!r.knob_node_id.empty()) {
        setKnobPosition(editor::NodeId::from_string(r.knob_node_id), r.knob_position, legacy_scope_id);
    }
    if (!r.toggle_probe_wire_id.empty()) {
        action.toggle_probe_wire_id = r.toggle_probe_wire_id;
        action.toggle_probe_scope_id = scope_id;
        action.has_toggle_probe_world_pos = r.has_toggle_probe_world_pos;
        action.toggle_probe_world_pos = r.toggle_probe_world_pos;
    }

    if (r.open_inline_value_editor && !r.inline_value_editor_node_id.empty()) {
        action.open_inline_value_editor = true;
        action.inline_value_editor_node_id = editor::NodeId::from_string(r.inline_value_editor_node_id);
        action.has_inline_value_editor_screen_pos = r.has_inline_value_editor_screen_pos;
        action.inline_value_editor_screen_pos = r.inline_value_editor_screen_pos;
    }

    return action;
}
