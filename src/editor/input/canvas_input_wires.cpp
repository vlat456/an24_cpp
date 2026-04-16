#include "input/canvas_input.h"
#include "visual/scene.h"
#include "visual/scene_mutations.h"
#include "visual/snap.h"
#include "visual/node/visual_node.h"
#include "visual/port/visual_port.h"
#include "visual/wire/wire.h"
#include "visual/wire/routing_point.h"
#include "editor/visual/presentation/canvas_scene_snapshot.h"
#include "viewport/viewport.h"
#include "commands/commands.h"
#include "canvas_input_internal.h"
#include "editor/common/port_type_utils.h"
#include "blueprint_v2/validation/wire_validator.h"
#include <algorithm>

using namespace canvas_input_impl;

namespace {

std::optional<Domain> reconcile_endpoint_domains(const bp2::PortDescriptor& src,
                                                 const bp2::PortDescriptor& tgt) {
    const bool src_any = (src.port_type == PortType::Any);
    const bool tgt_any = (tgt.port_type == PortType::Any);

    if (src_any && tgt_any) {
        return src.domain;
    }
    if (src_any) return tgt.domain;
    if (tgt_any) return src.domain;
    if (src.domain != tgt.domain) return std::nullopt;
    return src.domain;
}

Domain resolve_wire_domain_without_registry(const bp2::Blueprint& bp,
                                            ui::InternedId start_node,
                                            ui::InternedId start_port,
                                            ui::InternedId end_node,
                                            ui::InternedId end_port) {
    auto lookup_port = [&](ui::InternedId node_id, ui::InternedId port_id) -> std::optional<bp2::PortDescriptor> {
        const bp2::Blueprint::Node* node = bp.find_node(node_id);
        if (!node) return std::nullopt;
        for (const auto& p : bp.effective_node_iface(*node).ports()) {
            if (p.name == port_id) return p;
        }
        return std::nullopt;
    };

    const auto src = lookup_port(start_node, start_port);
    const auto tgt = lookup_port(end_node, end_port);
    if (!src || !tgt) {
        return Domain::Electrical;
    }

    const auto resolved = reconcile_endpoint_domains(*src, *tgt);
    return resolved.value_or(Domain::Electrical);
}

Domain resolve_wire_domain_from_endpoints(const bp2::Blueprint& bp,
                                          ui::InternedId start_node,
                                          ui::InternedId start_port,
                                          ui::InternedId end_node,
                                          ui::InternedId end_port,
                                          const TypeRegistry* parser_registry,
                                          ui::StringInterner& interner) {
    bp2::Blueprint::Wire probe;
    probe.source = bp2::WireEndpoint{start_node, start_port};
    probe.target = bp2::WireEndpoint{end_node, end_port};

    if (!parser_registry) {
        return resolve_wire_domain_without_registry(bp, start_node, start_port, end_node, end_port);
    }

    const auto result = bp2::WireValidator::validate(probe, bp, *parser_registry, interner);
    return result.resolved_domain;
}

}

InputResult CanvasInput::finish_wire_creation(Pt screen_pos, Pt canvas_min) {
    InputResult result;
    Pt world = viewport_.screen_to_world(screen_pos, canvas_min);
    auto port_hit = editor::presentation::hit_test_canvas_scene_ports(snapshot_, world, interner_);

    if (auto* ph = std::get_if<visual::HitPort>(&port_hit)) {
        if (!wire_start_endpoint_.has_value()) return result;

        CanvasInput::WireStartEndpoint start = *wire_start_endpoint_;
        ui::InternedId end_node_iid = interner_.intern(ph->node_id);
        ui::InternedId end_port_iid = interner_.intern(ph->port_name);

        if (start.node_id == end_node_iid && start.port_id == end_port_iid) return result;

        bool compatible = visual::Port::areSidesCompatible(start.side, ph->side);
        if (!compatible) return result;

        if (!visual::Port::areTypesCompatible(start.type, ph->type)) {
            return result;
        }

        std::string_view end_node_sv = ph->node_id;

        if (start.node_id.empty() || end_node_sv.empty()) return result;

        ui::InternedId start_node_iid = start.node_id;
        ui::InternedId start_port_iid = start.port_id;

        if (is_bus_node(host_.current_blueprint(), start_node_iid, registry(), interner_) && start_port_iid != interner_.intern("v")) {
            start_port_iid = interner_.intern("v");
        }
        if (is_bus_node(host_.current_blueprint(), end_node_iid, registry(), interner_) && end_port_iid != interner_.intern("v")) {
            end_port_iid = interner_.intern("v");
        }

        auto can_drive = [](bp2::PortSide s) {
            return s == bp2::PortSide::Output || s == bp2::PortSide::InOut;
        };
        auto can_receive = [](bp2::PortSide s) {
            return s == bp2::PortSide::Input || s == bp2::PortSide::InOut;
        };

        const bool forward_ok = can_drive(start.side) && can_receive(ph->side);
        const bool reverse_ok = can_drive(ph->side) && can_receive(start.side);
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
        w.domain = resolve_wire_domain_from_endpoints(
            host_.current_blueprint(),
            start_node_iid,
            start_port_iid,
            end_node_iid,
            end_port_iid,
            parser_registry_,
            interner_);

        bool added = host_.add_wire(std::move(w));
        if (added) {
            debug_validate_command_boundary(host_.current_blueprint(), interner_, arena_, parser_registry_);
            rebuild_scene();
            result.rebuild_simulation = true;
        }
    }
    return result;
}

InputResult CanvasInput::finish_wire_reconnection(Pt screen_pos, Pt canvas_min) {
      InputResult result;
      Pt world = viewport_.screen_to_world(screen_pos, canvas_min);
      auto port_hit = editor::presentation::hit_test_canvas_scene_ports(snapshot_, world, interner_);

     bool reconnected = false;

     const auto& wires = host_.wires();
     if (reconnect_wire_idx_ >= wires.size()) return result;

     const bp2::Blueprint::Wire& wire = wires[reconnect_wire_idx_];
     auto [wire_src_node, wire_src_port] = editor_math::path_to_node_port(wire.source, arena_);
     auto [wire_tgt_node, wire_tgt_port] = editor_math::path_to_node_port(wire.target, arena_);

     if (auto* ph = std::get_if<visual::HitPort>(&port_hit)) {
         std::string_view port_node_sv = ph->node_id;
         ui::InternedId port_node_iid  = interner_.intern(port_node_sv);
         ui::InternedId hit_port_iid   = interner_.intern(ph->port_name);

         ui::InternedId detached_node = reconnect_detach_start_ ? wire_src_node : wire_tgt_node;
         ui::InternedId detached_port = reconnect_detach_start_ ? wire_src_port : wire_tgt_port;
         ui::InternedId fixed_node    = reconnect_detach_start_ ? wire_tgt_node : wire_src_node;
         ui::InternedId fixed_port    = reconnect_detach_start_ ? wire_tgt_port : wire_src_port;

         bool same_as_original = (port_node_iid == detached_node && hit_port_iid == detached_port);
         if (same_as_original) return result;

          bool same_as_fixed = (port_node_iid == fixed_node && hit_port_iid == fixed_port);
          bool compatible = !same_as_fixed &&
              visual::Port::areSidesCompatible(ph->side, reconnect_fixed_side_);
         if (compatible && !visual::Port::areTypesCompatible(ph->type, reconnect_fixed_type_)) {
              compatible = false;
          }

         if (is_bus_node(host_.current_blueprint(), port_node_iid, registry(), interner_) && is_wire_alias_port_name(ph->port_name)) {
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
                      rebuild_scene();
                      result.rebuild_simulation = true;
                      reconnected = true;
                 } else {
                     reconnected = true;
                 }
             }
         } else if (compatible) {
              ui::InternedId new_node_iid = interner_.intern(port_node_sv);
              ui::InternedId new_port_iid = interner_.intern(ph->port_name);

             if (is_bus_node(host_.current_blueprint(), new_node_iid, registry(), interner_) && new_port_iid != interner_.intern("v")) {
                 new_port_iid = interner_.intern("v");
             }

             bp2::WireEndpoint new_ep{new_node_iid, new_port_iid};
             const Domain new_domain = reconnect_detach_start_
                 ? resolve_wire_domain_from_endpoints(
                       host_.current_blueprint(),
                       new_node_iid,
                       new_port_iid,
                       fixed_node,
                       fixed_port,
                       parser_registry_,
                       interner_)
                 : resolve_wire_domain_from_endpoints(
                       host_.current_blueprint(),
                       fixed_node,
                       fixed_port,
                       new_node_iid,
                       new_port_iid,
                       parser_registry_,
                       interner_);

              bool updated_ok = host_.update_wire(wire.id, [&](bp2::Blueprint::Wire& wr) {
                  if (reconnect_detach_start_) {
                      wr.source = new_ep;
                  } else {
                      wr.target = new_ep;
                  }
                  wr.domain = new_domain;
              });

              if (updated_ok) {
                  debug_validate_command_boundary(host_.current_blueprint(), interner_, arena_, parser_registry_);
                  rebuild_scene();
                  result.rebuild_simulation = true;
                  reconnected = true;
              }
         }
     }

      if (!reconnected && reconnect_wire_idx_ < host_.wires().size()) {
          if (host_.remove_wire(wire.id)) {
              rebuild_scene();
              result.rebuild_simulation = true;
          }
      }

     return result;
}

std::optional<CanvasInput::WirePortMatch> CanvasInput::find_wire_on_port(
     ui::InternedId port_node_iid, ui::InternedId port_name_iid) const {
     if (port_node_iid.empty() || port_name_iid.empty()) return std::nullopt;

     std::string_view port_name_sv = interner_.resolve(port_name_iid);

     if (is_bus_node(host_.current_blueprint(), port_node_iid, registry(), interner_)) {
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

     // Estimate a port's world position from blueprint layout data.
     // The exact pixel position depends on widget layout, but this is only
     // used as the reconnection anchor (visual wire preview start point),
     // so an approximate center-of-node position is sufficient.
     auto estimate_port_pos = [&](ui::InternedId node_id, ui::InternedId port_id) -> std::optional<Pt> {
         (void)port_id;
         const bp2::Blueprint::Node* node = host_.find_node(node_id);
         if (!node) return std::nullopt;
         constexpr float DEFAULT_W = 64.0f;
         constexpr float DEFAULT_H = 32.0f;
         float w = node->layout.width.value_or(DEFAULT_W);
         float h = node->layout.height.value_or(DEFAULT_H);
         return Pt(node->layout.x + w * 0.5f, node->layout.y + h * 0.5f);
     };

     if (detach_start) {
         fixed_side = bp2::PortSide::Input;
         auto [tgt_node, tgt_port] = editor_math::path_to_node_port(w.target, arena_);
         fixed_type = resolve_port_type_from_model(host_.current_blueprint(), tgt_node, tgt_port);
         if (!w.routing_points.empty()) {
             anchor_pos = Pt(w.routing_points.front().first, w.routing_points.front().second);
         } else if (auto pos = estimate_port_pos(tgt_node, tgt_port)) {
             anchor_pos = *pos;
         }
     } else {
         fixed_side = bp2::PortSide::Output;
         auto [src_node, src_port] = editor_math::path_to_node_port(w.source, arena_);
         fixed_type = resolve_port_type_from_model(host_.current_blueprint(), src_node, src_port);
         if (!w.routing_points.empty()) {
             anchor_pos = Pt(w.routing_points.back().first, w.routing_points.back().second);
         } else if (auto pos = estimate_port_pos(src_node, src_port)) {
             anchor_pos = *pos;
         }
     }
     return WirePortMatch{wire_index, detach_start, anchor_pos, fixed_side, fixed_type};
}
