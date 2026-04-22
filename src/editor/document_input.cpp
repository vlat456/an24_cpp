#include "document.h"
#include "identity.h"

Document::InputResultAction Document::applyInputResult(const InputResult& r,
                                                        const WindowScopeId& scope_id) {
    InputResultAction action;

    if (r.rebuild_simulation) {
        rebuildSimulation();
        window_manager_.remove_orphaned_windows();
    }
    if (r.show_context_menu) {
        action.show_context_menu = true;
        action.context_menu_pos = r.context_menu_pos;
        action.context_menu_scope_id = scope_id;
    }
    if (r.show_node_context_menu) {
        action.show_node_context_menu = true;
        action.context_menu_node_id = editor::NodeId::from_string(r.context_menu_node_id);
        action.node_context_menu_scope_id = scope_id;
    }
    if (!r.open_sub_window.empty()) {
        openSubWindow(scope_id, r.open_sub_window);
    }
    if (!r.toggle_switch_node_id.empty()) {
        triggerSwitch(editor::NodeId::from_string(r.toggle_switch_node_id), scope_id);
    }
    if (!r.slider_node_id.empty()) {
        setSliderValue(editor::NodeId::from_string(r.slider_node_id), r.slider_value, scope_id);
    }
    if (!r.knob_node_id.empty()) {
        setKnobPosition(editor::NodeId::from_string(r.knob_node_id), r.knob_position, scope_id);
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
        action.inline_value_editor_scope_id = scope_id;
        action.has_inline_value_editor_screen_pos = r.has_inline_value_editor_screen_pos;
        action.inline_value_editor_screen_pos = r.inline_value_editor_screen_pos;
    }

    return action;
}
