#include "input/canvas_input.h"
#include "visual/scene.h"
#include "visual/scene_hittest.h"
#include "visual/scene_mutations.h"
#include "visual/snap.h"
#include "visual/node/visual_node.h"
#include "visual/port/visual_port.h"
#include "visual/wire/wire.h"
#include "visual/wire/routing_point.h"
#include "viewport/viewport.h"
#include "commands/commands.h"
#include "canvas_input_internal.h"
#include <algorithm>

using namespace canvas_input_impl;

InputResult CanvasInput::finish_wire_creation(Pt screen_pos, Pt canvas_min) {
    InputResult result;
    Pt world = viewport_.screen_to_world(screen_pos, canvas_min);
    auto port_hit = visual::hit_test_ports(scene_, world);

    if (auto* ph = std::get_if<visual::HitPort>(&port_hit)) {
        if (!wire_start_port_ || ph->port == wire_start_port_) return result;

        visual::Port* start_port = wire_start_port_;
        visual::Port* end_port = ph->port;

        bool compatible = visual::Port::areSidesCompatible(start_port->side(), end_port->side());
        if (!compatible) return result;

        if (!visual::Port::areTypesCompatible(start_port->type(), end_port->type())) {
            return result;
        }

        std::string_view start_node_sv = start_port->rootAncestorId();
        std::string_view end_node_sv = end_port->rootAncestorId();

        if (start_node_sv.empty() || end_node_sv.empty()) return result;

        if (start_node_sv == end_node_sv &&
            start_port->name() == end_port->name()) return result;

        ui::InternedId start_node_iid = interner_.intern(start_node_sv);
        ui::InternedId start_port_iid = interner_.intern(start_port->name());
        ui::InternedId end_node_iid   = interner_.intern(end_node_sv);
        ui::InternedId end_port_iid   = interner_.intern(end_port->name());

         if (is_bus_node(host_.current_blueprint(), start_node_iid) && start_port_iid != interner_.intern("v")) {
             start_port_iid = interner_.intern("v");
         }
         if (is_bus_node(host_.current_blueprint(), end_node_iid) && end_port_iid != interner_.intern("v")) {
             end_port_iid = interner_.intern("v");
         }

         auto can_drive = [](bp2::PortSide s) {
             return s == bp2::PortSide::Output || s == bp2::PortSide::InOut;
         };
         auto can_receive = [](bp2::PortSide s) {
             return s == bp2::PortSide::Input || s == bp2::PortSide::InOut;
         };

        const bool forward_ok = can_drive(start_port->side()) && can_receive(end_port->side());
        const bool reverse_ok = can_drive(end_port->side()) && can_receive(start_port->side());
        if (!forward_ok && !reverse_ok) {
            return result;
        }
        if (!forward_ok && reverse_ok) {
            std::swap(start_node_iid, end_node_iid);
            std::swap(start_port_iid, end_port_iid);
        }

         std::string wire_id_str = host_.allocate_wire_id();
         ui::InternedId wire_iid = interner_.intern(wire_id_str);

         bp2::Blueprint::Wire w;
         w.id     = wire_iid;
         w.source = bp2::WireEndpoint{start_node_iid, start_port_iid};
         w.target = bp2::WireEndpoint{end_node_iid, end_port_iid};

         bool added = host_.add_wire(std::move(w));
         if (added) {
             debug_validate_command_boundary(host_.current_blueprint(), interner_, arena_, parser_registry_);
             visual::mutations::rebuild(scene_, host_.current_blueprint(), interner_, arena_, scope_id_);
             result.rebuild_simulation = true;
         }
    }
    return result;
}

InputResult CanvasInput::finish_wire_reconnection(Pt screen_pos, Pt canvas_min) {
     InputResult result;
     Pt world = viewport_.screen_to_world(screen_pos, canvas_min);
     auto port_hit = visual::hit_test_ports(scene_, world);

     bool reconnected = false;

     const auto& wires = host_.wires();
     if (reconnect_wire_idx_ >= wires.size()) return result;

     const bp2::Blueprint::Wire& wire = wires[reconnect_wire_idx_];
     auto [wire_src_node, wire_src_port] = editor_math::path_to_node_port(wire.source, arena_);
     auto [wire_tgt_node, wire_tgt_port] = editor_math::path_to_node_port(wire.target, arena_);

     if (auto* ph = std::get_if<visual::HitPort>(&port_hit)) {
         std::string_view port_node_sv = ph->port->rootAncestorId();
         ui::InternedId port_node_iid  = interner_.intern(port_node_sv);
         ui::InternedId hit_port_iid   = interner_.intern(ph->port->name());

         ui::InternedId detached_node = reconnect_detach_start_ ? wire_src_node : wire_tgt_node;
         ui::InternedId detached_port = reconnect_detach_start_ ? wire_src_port : wire_tgt_port;
         ui::InternedId fixed_node    = reconnect_detach_start_ ? wire_tgt_node : wire_src_node;
         ui::InternedId fixed_port    = reconnect_detach_start_ ? wire_tgt_port : wire_src_port;

         bool same_as_original = (port_node_iid == detached_node && hit_port_iid == detached_port);
         if (same_as_original) return result;

          bool same_as_fixed = (port_node_iid == fixed_node && hit_port_iid == fixed_port);
          bool compatible = !same_as_fixed &&
              visual::Port::areSidesCompatible(ph->port->side(), reconnect_fixed_side_);
         if (compatible && !visual::Port::areTypesCompatible(ph->port->type(), reconnect_fixed_type_)) {
             compatible = false;
         }

         if (is_bus_node(host_.current_blueprint(), port_node_iid) && is_wire_alias_port_name(ph->port->name())) {
             size_t target_wire_idx = find_wire_index(hit_port_iid);
             if (target_wire_idx != SIZE_MAX && target_wire_idx < wires.size()) {
                 if (target_wire_idx != reconnect_wire_idx_) {
                     auto reordered = wires;
                     std::swap(reordered[reconnect_wire_idx_], reordered[target_wire_idx]);

                     bp2::Blueprint updated_bp = host_.current_blueprint();
                     for (const auto& old_wire : host_.current_blueprint().wires()) {
                         updated_bp = updated_bp.without_wire(old_wire.id);
                     }
                     for (const auto& new_wire : reordered) {
                         updated_bp = updated_bp.with_wire(new_wire);
                     }

                      host_.mutate_atomically([&] {
                          host_.replace_current(std::move(updated_bp));
                      });
                     debug_validate_command_boundary(host_.current_blueprint(), interner_, arena_, parser_registry_);
                     visual::mutations::rebuild(scene_, host_.current_blueprint(), interner_, arena_, scope_id_);
                     result.rebuild_simulation = true;
                     reconnected = true;
                 } else {
                     reconnected = true;
                 }
             }
         } else if (compatible) {
             ui::InternedId new_node_iid = interner_.intern(port_node_sv);
             ui::InternedId new_port_iid = interner_.intern(ph->port->name());

             if (is_bus_node(host_.current_blueprint(), new_node_iid) && new_port_iid != interner_.intern("v")) {
                 new_port_iid = interner_.intern("v");
             }

             bp2::WireEndpoint new_ep{new_node_iid, new_port_iid};

             bool updated_ok = host_.update_wire(wire.id, [&](bp2::Blueprint::Wire& wr) {
                 if (reconnect_detach_start_) {
                     wr.source = new_ep;
                 } else {
                     wr.target = new_ep;
                 }
             });

             if (updated_ok) {
                 debug_validate_command_boundary(host_.current_blueprint(), interner_, arena_, parser_registry_);
                 visual::mutations::rebuild(scene_, host_.current_blueprint(), interner_, arena_, scope_id_);
                 result.rebuild_simulation = true;
                 reconnected = true;
             }
         }
     }

     if (!reconnected && reconnect_wire_idx_ < host_.wires().size()) {
         if (host_.remove_wire(wire.id)) {
             visual::mutations::rebuild(scene_, host_.current_blueprint(), interner_, arena_, scope_id_);
             result.rebuild_simulation = true;
         }
     }

     return result;
}

std::optional<CanvasInput::WirePortMatch> CanvasInput::find_wire_on_port(visual::Port* port) const {
     if (!port) return std::nullopt;

     std::string_view port_node_sv = port->rootAncestorId();
     std::string_view port_name_sv = port->name();

     ui::InternedId port_node_iid = interner_.intern(port_node_sv);
     ui::InternedId port_name_iid = interner_.intern(port_name_sv);

     if (is_bus_node(host_.current_blueprint(), port_node_iid)) {
         if (port_name_sv == "v") {
             return std::nullopt;
         }

         if (is_wire_alias_port_name(port_name_sv)) {
             size_t wi = find_wire_index(port_name_iid);
             if (wi == SIZE_MAX) return std::nullopt;
             const bp2::Blueprint::Wire& w = host_.wires()[wi];

             auto [src_node, _src_port] = editor_math::path_to_node_port(w.source, arena_);
             auto [tgt_node, _tgt_port] = editor_math::path_to_node_port(w.target, arena_);
             if (src_node == port_node_iid) return build_wire_port_match(wi, true, w);
             if (tgt_node == port_node_iid) return build_wire_port_match(wi, false, w);
         }
         return std::nullopt;
     }

     const auto& wires = host_.wires();
     for (size_t wi = 0; wi < wires.size(); ++wi) {
         const bp2::Blueprint::Wire& w = wires[wi];
         auto [src_node, src_port] = editor_math::path_to_node_port(w.source, arena_);
         auto [tgt_node, tgt_port] = editor_math::path_to_node_port(w.target, arena_);

         if (src_node == port_node_iid && src_port == port_name_iid) {
             return build_wire_port_match(wi, true, w);
         }
         if (tgt_node == port_node_iid && tgt_port == port_name_iid) {
             return build_wire_port_match(wi, false, w);
         }
     }

     return std::nullopt;
}

CanvasInput::WirePortMatch CanvasInput::build_wire_port_match(
     size_t wire_index, bool detach_start, const bp2::Blueprint::Wire& w) const {
     Pt anchor_pos;
     bp2::PortSide fixed_side;
     PortType fixed_type = PortType::Any;
     if (detach_start) {
         fixed_side = bp2::PortSide::Input;
         auto [tgt_node, tgt_port] = editor_math::path_to_node_port(w.target, arena_);
         fixed_type = resolve_port_type_from_model(host_.current_blueprint(), tgt_node, tgt_port);
         if (!w.routing_points.empty()) {
             anchor_pos = Pt(w.routing_points.front().first, w.routing_points.front().second);
         } else {
             auto* end_widget = scene_.find(interner_.resolve(tgt_node));
             if (end_widget) {
                 auto* end_port = end_widget->portByName(interner_.resolve(tgt_port), interner_.resolve(w.id));
                 if (end_port) {
                     anchor_pos = end_port->worldPos() + Pt(visual::PortConstants::RADIUS, visual::PortConstants::RADIUS);
                     fixed_type = end_port->type();
                 }
             }
         }
     } else {
         fixed_side = bp2::PortSide::Output;
         auto [src_node, src_port] = editor_math::path_to_node_port(w.source, arena_);
         fixed_type = resolve_port_type_from_model(host_.current_blueprint(), src_node, src_port);
         if (!w.routing_points.empty()) {
             anchor_pos = Pt(w.routing_points.back().first, w.routing_points.back().second);
         } else {
             auto* start_widget = scene_.find(interner_.resolve(src_node));
             if (start_widget) {
                 auto* start_port = start_widget->portByName(interner_.resolve(src_port), interner_.resolve(w.id));
                 if (start_port) {
                     anchor_pos = start_port->worldPos() + Pt(visual::PortConstants::RADIUS, visual::PortConstants::RADIUS);
                     fixed_type = start_port->type();
                 }
             }
         }
     }
     return WirePortMatch{wire_index, detach_start, anchor_pos, fixed_side, fixed_type};
}
