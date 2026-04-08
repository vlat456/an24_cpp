#include "document.h"

Document::InputResultAction Document::applyInputResult(const InputResult& r,
                                                        const std::string& group_id) {
    InputResultAction action;

    if (!group_id.empty()) {
        BlueprintWindow* win = window_manager_.find(group_id);
        if (win && win->embedded_model) {
            const ui::InternedId nested_iid = interner_.lookup(group_id);
            const bp2::Blueprint::Nested* nested = nested_iid.empty()
                ? nullptr
                : model_.current().find_nested(nested_iid);
            if (nested && nested->inline_def
                && *nested->inline_def != win->embedded_model->current()) {
                bp2::Blueprint::Nested updated = *nested;
                updated.inline_def = std::make_unique<bp2::Blueprint>(win->embedded_model->current());
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
        action.context_menu_group_id = group_id;
    }
    if (r.show_node_context_menu) {
        action.show_node_context_menu = true;
        action.context_menu_node_id = r.context_menu_node_id;
        action.node_context_menu_group_id = group_id;
    }
    if (!r.open_sub_window.empty()) {
        openSubWindow(r.open_sub_window);
    }
    if (!r.toggle_switch_node_id.empty()) {
        triggerSwitch(r.toggle_switch_node_id, group_id);
    }
    if (!r.slider_node_id.empty()) {
        setSliderValue(r.slider_node_id, r.slider_value, group_id);
    }
    if (!r.knob_node_id.empty()) {
        setKnobPosition(r.knob_node_id, r.knob_position, group_id);
    }
    if (!r.toggle_probe_wire_id.empty()) {
        action.toggle_probe_wire_id = r.toggle_probe_wire_id;
        action.toggle_probe_group_id = group_id;
        action.has_toggle_probe_world_pos = r.has_toggle_probe_world_pos;
        action.toggle_probe_world_pos = r.toggle_probe_world_pos;
    }

    if (r.open_inline_value_editor && !r.inline_value_editor_node_id.empty()) {
        action.open_inline_value_editor = true;
        action.inline_value_editor_node_id = r.inline_value_editor_node_id;
        action.has_inline_value_editor_screen_pos = r.has_inline_value_editor_screen_pos;
        action.inline_value_editor_screen_pos = r.inline_value_editor_screen_pos;
    }

    return action;
}
