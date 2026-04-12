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
                result.toggle_probe_wire_id = std::string(hw->wire->id());
                result.has_toggle_probe_world_pos = true;
                result.toggle_probe_world_pos = world;
                return result;
            }
        }

        if (read_only) {
            auto hit = visual::hit_test(scene_, world);
            if (auto* h = std::get_if<visual::HitNode>(&hit)) {
                if (!mods.ctrl) clear_selection();
                add_node_selection(h->widget);
            } else {
                clear_selection_and_enter_panning();
            }
            return result;
        }

        if (simulation_mode) {
            auto hit = visual::hit_test(scene_, world);
            if (auto* hn = std::get_if<visual::HitNode>(&hit)) {
                auto content_target = hit_test_semantic_content(hn->widget, world);
                if (content_target.has_value()) {
                    InputResult ir;
                    if (handle_resolved_interaction(hn->widget, *content_target, world, ir)) {
                        return ir;
                    }
                }
            }
            clear_selection_and_enter_panning();
            return result;
        }

        auto port_hit = visual::hit_test_ports(scene_, world);
        if (auto* ph = std::get_if<visual::HitPort>(&port_hit)) {
            auto wire_match = find_wire_on_port(ph->port);
            if (wire_match) {
                enter_reconnect_wire(wire_match->wire_index, wire_match->detach_start,
                                     wire_match->anchor_pos, wire_match->fixed_side,
                                     wire_match->fixed_type);
                return result;
            }
            Pt port_center = ph->port->worldPos() + Pt(visual::PortConstants::RADIUS, visual::PortConstants::RADIUS);
            enter_create_wire(ph->port, port_center);
            return result;
        }

        auto hit = visual::hit_test(scene_, world);

        if (mods.alt) {
            enter_marquee(world);
        } else if (auto* hrh = std::get_if<visual::HitResizeHandle>(&hit)) {
            enter_resize_node(hrh->widget, hrh->corner);
         } else if (auto* hn = std::get_if<visual::HitNode>(&hit)) {
             auto content_target = hit_test_semantic_content(hn->widget, world);
             if (content_target.has_value()) {
                 if (handle_resolved_interaction(hn->widget, *content_target, world, result)) {
                     return result;
                 }
             }
             enter_drag_node(hn->widget, false, mods.ctrl);
        } else if (auto* hrp = std::get_if<visual::HitRoutingPoint>(&hit)) {
            enter_drag_routing_point(hrp->wire, hrp->point, hrp->index);
        } else if (auto* hw = std::get_if<visual::HitWire>(&hit)) {
            clear_selection();
            selected_wire_id_ = interner_.intern(hw->wire->id());
        } else {
            clear_selection_and_enter_panning();
        }
    } else if (btn == MouseButton::Right && !read_only && !simulation_mode) {
        auto hit = visual::hit_test(scene_, world);
        if (auto* hn = std::get_if<visual::HitNode>(&hit)) {
            result.show_node_context_menu = true;
            result.context_menu_node_id = std::string(hn->widget->id());
        } else if (std::holds_alternative<visual::HitEmpty>(hit)) {
            result.show_context_menu = true;
            result.context_menu_pos = world;
        }
    }
    return result;
}
