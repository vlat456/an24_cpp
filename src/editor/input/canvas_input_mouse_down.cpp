#include "input/canvas_input.h"
#include "visual/scene.h"
#include "visual/scene_hittest.h"
#include "visual/snap.h"
#include "visual/node/visual_node.h"
#include "visual/port/visual_port.h"
#include "visual/wire/wire.h"
#include "visual/wire/routing_point.h"
#include "viewport/viewport.h"
#include "canvas_input_internal.h"

using namespace canvas_input_impl;

InputResult CanvasInput::on_mouse_down(Pt screen_pos, MouseButton btn, Pt canvas_min, Modifiers mods) {
    InputResult result;
    Pt world = viewport_.screen_to_world(screen_pos, canvas_min);
    last_world_pos_ = world;

    if (btn == MouseButton::Left) {
        if (mods.shift) {
            auto hit = visual::hit_test(scene_, world);
            if (auto* hw = std::get_if<visual::HitWire>(&hit)) {
                result.toggle_probe_wire_id = std::string(hw->wire_id);
                result.has_toggle_probe_world_pos = true;
                result.toggle_probe_world_pos = world;
                return result;
            }
        }

        if (read_only) {
            auto hit = visual::hit_test(scene_, world);
            if (auto* h = std::get_if<visual::HitNode>(&hit)) {
                if (!mods.ctrl) clear_selection();
                add_node_selection(interner_.intern(h->node_id));
            } else {
                clear_selection_and_enter_panning();
            }
            return result;
        }

        if (simulation_mode) {
            auto hit = visual::hit_test(scene_, world);
            if (auto* hn = std::get_if<visual::HitNode>(&hit)) {
                ui::InternedId node_id = interner_.intern(hn->node_id);
                auto content_target = hit_test_semantic_content(node_id, world);
                if (content_target.has_value()) {
                    InputResult ir;
                    if (handle_resolved_interaction(node_id, *content_target, world, ir)) {
                        return ir;
                    }
                }
            }
            clear_selection_and_enter_panning();
            return result;
        }

        auto port_hit = visual::hit_test_ports(scene_, world);
        if (auto* ph = std::get_if<visual::HitPort>(&port_hit)) {
            ui::InternedId port_node_iid = interner_.intern(ph->node_id);
            ui::InternedId port_name_iid = interner_.intern(ph->port_name);
            auto wire_match = find_wire_on_port(port_node_iid, port_name_iid);
            if (wire_match) {
                enter_reconnect_wire(wire_match->wire_index, wire_match->detach_start,
                                     wire_match->anchor_pos, wire_match->fixed_side,
                                     wire_match->fixed_type);
                return result;
            }
            Pt port_center = ph->center;
            enter_create_wire(port_node_iid,
                              port_name_iid,
                              ph->side,
                              ph->type,
                              port_center);
            return result;
        }

        auto hit = visual::hit_test(scene_, world);

        if (mods.alt) {
            enter_marquee(world);
        } else if (auto* hrh = std::get_if<visual::HitResizeHandle>(&hit)) {
            enter_resize_node(interner_.intern(hrh->node_id), hrh->world_pos, hrh->size, hrh->corner);
         } else if (auto* hn = std::get_if<visual::HitNode>(&hit)) {
             ui::InternedId node_id = interner_.intern(hn->node_id);
             auto content_target = hit_test_semantic_content(node_id, world);
             if (content_target.has_value()) {
                 if (handle_resolved_interaction(node_id, *content_target, world, result)) {
                     return result;
                 }
             }
             enter_drag_node(node_id, hn->world_pos, mods.ctrl);
        } else if (auto* hrp = std::get_if<visual::HitRoutingPoint>(&hit)) {
            enter_drag_routing_point(interner_.intern(hrp->wire_id), hrp->index,
                                     hrp->world_pos);
        } else if (auto* hw = std::get_if<visual::HitWire>(&hit)) {
            clear_selection();
            selected_wire_id_ = interner_.intern(hw->wire_id);
        } else {
            clear_selection_and_enter_panning();
        }
    } else if (btn == MouseButton::Right && !read_only && !simulation_mode) {
        auto hit = visual::hit_test(scene_, world);
        if (auto* hn = std::get_if<visual::HitNode>(&hit)) {
            result.show_node_context_menu = true;
            result.context_menu_node_id = std::string(hn->node_id);
        } else if (std::holds_alternative<visual::HitEmpty>(hit)) {
            result.show_context_menu = true;
            result.context_menu_pos = world;
        }
    }
    return result;
}
