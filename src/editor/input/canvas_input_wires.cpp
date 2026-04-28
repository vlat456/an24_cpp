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
#include "editor/common/port_type_utils.h"
#include <algorithm>

namespace {

/// Bus nodes have a single logical port "v"; all other port names are aliases.
inline bool is_wire_alias_port_name(std::string_view port_name) {
    return !port_name.empty() && port_name != "v";
}

/// Resolve effective port type, falling back to visual-layer type when
/// the model has no opinion (PortType::Any).
PortType resolve_effective_port_type(EditingHost& host,
                                     core::InternedId node_id,
                                     core::InternedId port_name,
                                     PortType fallback_type) {
    const PortType model_type = host.resolve_port_type(node_id, port_name);
    return model_type == PortType::Any ? fallback_type : model_type;
}

} // namespace

InputResult CanvasInput::finish_wire_creation(Pt screen_pos, Pt canvas_min) {
    InputResult result;
    Pt world = viewport_.screen_to_world(screen_pos, canvas_min);
    auto port_hit = editor::presentation::hit_test_canvas_scene_ports(snapshot_, world);

    if (auto* ph = std::get_if<visual::HitPort>(&port_hit)) {
        if (!wire_start_endpoint_.has_value()) return result;

        CanvasInput::WireStartEndpoint start = *wire_start_endpoint_;
        core::InternedId end_node_iid = ph->node_id;
        core::InternedId end_port_iid = ph->port_name;

        if (start.node_id == end_node_iid && start.port_id == end_port_iid) return result;

        bool compatible = visual::Port::areDirectionsCompatible(start.direction, ph->direction);
        if (!compatible) return result;

        if (!visual::Port::areTypesCompatible(start.type, ph->type)) {
            return result;
        }

        if (start.node_id.empty() || end_node_iid.empty()) return result;

        core::InternedId start_node_iid = start.node_id;
        core::InternedId start_port_iid = start.port_id;

        // Canonicalize bus ports: all bus-node ports alias to "v".
        if (host_->resolve_frame_kind(start_node_iid) == editor::presentation::NodeFrameKind::Bus
            && start_port_iid != interner_->intern("v")) {
            start_port_iid = interner_->intern("v");
        }
        if (host_->resolve_frame_kind(end_node_iid) == editor::presentation::NodeFrameKind::Bus
            && end_port_iid != interner_->intern("v")) {
            end_port_iid = interner_->intern("v");
        }

        auto can_drive = [](bp2::Direction s) {
            return s == bp2::Direction::Output || s == bp2::Direction::InOut;
        };
        auto can_receive = [](bp2::Direction s) {
            return s == bp2::Direction::Input || s == bp2::Direction::InOut;
        };

        const bool forward_ok = can_drive(start.direction) && can_receive(ph->direction);
        const bool reverse_ok = can_drive(ph->direction) && can_receive(start.direction);
        if (!forward_ok && !reverse_ok) {
            return result;
        }
        if (!forward_ok && reverse_ok) {
            std::swap(start_node_iid, end_node_iid);
            std::swap(start_port_iid, end_port_iid);
        }

        std::string wire_id_str = host_->allocate_wire_id();
        core::InternedId wire_iid = interner_->intern(wire_id_str);

        bp2::Blueprint::Wire w;
        w.id     = wire_iid;
        w.source = bp2::WireEndpoint{start_node_iid, start_port_iid};
        w.target = bp2::WireEndpoint{end_node_iid, end_port_iid};
        w.domain = host_->resolve_wire_domain(w.source, w.target);

        bool added = host_->add_wire(std::move(w));
        if (added) {
            host_->debug_validate_integrity();
            rebuild_scene();
            result.rebuild_simulation = true;
        }
    }
    return result;
}

InputResult CanvasInput::finish_wire_reconnection(Pt screen_pos, Pt canvas_min) {
    InputResult result;
    Pt world = viewport_.screen_to_world(screen_pos, canvas_min);
    auto port_hit = editor::presentation::hit_test_canvas_scene_ports(snapshot_, world);

    bool reconnected = false;

    const auto& wires = host_->wires();
    if (reconnect_wire_idx_ >= wires.size()) return result;

    const bp2::Blueprint& bp = host_->current_blueprint();
    const bp2::Blueprint::Wire& wire = wires[reconnect_wire_idx_];
    auto [wire_src_node, wire_src_port] = editor_math::path_to_node_port(wire.source, *arena_);
    auto [wire_tgt_node, wire_tgt_port] = editor_math::path_to_node_port(wire.target, *arena_);

    if (auto* ph = std::get_if<visual::HitPort>(&port_hit)) {
        const core::InternedId port_node_iid = ph->node_id;
        const core::InternedId hit_port_iid = ph->port_name;
        const PortType hit_port_type = resolve_effective_port_type(
            *host_,
            port_node_iid,
            hit_port_iid,
            ph->type);

        const core::InternedId detached_node = reconnect_detach_start_ ? wire_src_node : wire_tgt_node;
        const core::InternedId detached_port = reconnect_detach_start_ ? wire_src_port : wire_tgt_port;
        const core::InternedId fixed_node = reconnect_detach_start_ ? wire_tgt_node : wire_src_node;
        const core::InternedId fixed_port = reconnect_detach_start_ ? wire_tgt_port : wire_src_port;

        bp2::Direction fixed_direction = reconnect_fixed_direction_;
        PortType fixed_type = reconnect_fixed_type_;
        if (const auto fixed_desc = host_->resolve_port_descriptor(fixed_node, fixed_port)) {
            fixed_direction = fixed_desc->direction;
            fixed_type = fixed_desc->port_type;
        }

        const bool same_as_original = port_node_iid == detached_node && hit_port_iid == detached_port;
        if (same_as_original) return result;

        const bool same_as_fixed = port_node_iid == fixed_node && hit_port_iid == fixed_port;
        bool compatible = !same_as_fixed &&
            visual::Port::areDirectionsCompatible(ph->direction, fixed_direction);
        if (compatible && !visual::Port::areTypesCompatible(hit_port_type, fixed_type)) {
            compatible = false;
        }
        if (same_as_fixed || !compatible) {
            return result;
        }

        // Bus alias reordering: when reconnecting onto a bus alias port,
        // reorder the wires array instead of changing wire endpoints.
        const bool is_bus_hit = host_->resolve_frame_kind(port_node_iid) == editor::presentation::NodeFrameKind::Bus;
        if (is_bus_hit && is_wire_alias_port_name(interner_->resolve(ph->port_name))) {
            const size_t target_wire_idx = find_wire_index(hit_port_iid);
            if (target_wire_idx != SIZE_MAX && target_wire_idx < wires.size()) {
                if (target_wire_idx != reconnect_wire_idx_) {
                    auto reordered = wires;
                    std::swap(reordered[reconnect_wire_idx_], reordered[target_wire_idx]);

                    bp2::Blueprint updated_bp = bp;
                    for (const auto& old_wire : bp.wires()) {
                        updated_bp = updated_bp.without_wire(old_wire.id);
                    }
                    for (const auto& new_wire : reordered) {
                        updated_bp = updated_bp.with_wire(new_wire);
                    }

                    host_->mutate_atomically([&] {
                        host_->replace_current(std::move(updated_bp));
                    });
                    host_->debug_validate_integrity();
                    rebuild_scene();
                    result.rebuild_simulation = true;
                }
                reconnected = true;
            }
        } else {
            // Standard reconnection: canonicalize bus port if needed.
            const core::InternedId new_node_iid = port_node_iid;
            core::InternedId new_port_iid = hit_port_iid;
            if (host_->resolve_frame_kind(new_node_iid) == editor::presentation::NodeFrameKind::Bus) {
                new_port_iid = interner_->intern("v");
            }

            // Validate the new wire before committing.
            const bp2::WireEndpoint candidate_ep{new_node_iid, new_port_iid};
            const bp2::WireEndpoint fixed_ep{fixed_node, fixed_port};
            const bool valid = reconnect_detach_start_
                ? host_->validate_wire(candidate_ep, fixed_ep).valid
                : host_->validate_wire(fixed_ep, candidate_ep).valid;
            if (!valid) return result;

            const Domain new_domain = reconnect_detach_start_
                ? host_->resolve_wire_domain(candidate_ep, fixed_ep)
                : host_->resolve_wire_domain(fixed_ep, candidate_ep);

            const bool updated_ok = host_->update_wire(wire.id, [&](bp2::Blueprint::Wire& wr) {
                if (reconnect_detach_start_) {
                    wr.source = candidate_ep;
                } else {
                    wr.target = candidate_ep;
                }
                wr.domain = new_domain;
            });

            if (updated_ok) {
                host_->debug_validate_integrity();
                rebuild_scene();
                result.rebuild_simulation = true;
                reconnected = true;
            }
        }
    }

    if (!reconnected && reconnect_wire_idx_ < host_->wires().size()) {
        if (host_->remove_wire(wire.id)) {
            rebuild_scene();
            result.rebuild_simulation = true;
        }
    }

    return result;
}

std::optional<CanvasInput::WirePortMatch> CanvasInput::find_wire_on_port(
    core::InternedId port_node_iid, core::InternedId port_name_iid) const {
    if (port_node_iid.empty() || port_name_iid.empty()) return std::nullopt;

    std::string_view port_name_sv = interner_->resolve(port_name_iid);

    // Bus nodes: wires are keyed by the canonical "v" port.
    // Alias port names map to the wire whose ID matches the alias.
    if (host_->resolve_frame_kind(port_node_iid) == editor::presentation::NodeFrameKind::Bus) {
        if (port_name_sv == "v") {
            return std::nullopt;
        }

        if (is_wire_alias_port_name(port_name_sv)) {
            size_t wi = find_wire_index(port_name_iid);
            if (wi == SIZE_MAX) return std::nullopt;
            const bp2::Blueprint::Wire& w = host_->wires()[wi];

            auto [src_node, _src_port] = editor_math::path_to_node_port(w.source, *arena_);
            auto [tgt_node, _tgt_port] = editor_math::path_to_node_port(w.target, *arena_);
            if (src_node == port_node_iid) return build_wire_port_match(wi, true, w);
            if (tgt_node == port_node_iid) return build_wire_port_match(wi, false, w);
        }
        return std::nullopt;
    }

    const auto& wires = host_->wires();
    for (size_t wi = 0; wi < wires.size(); ++wi) {
        const bp2::Blueprint::Wire& w = wires[wi];
        auto [src_node, src_port] = editor_math::path_to_node_port(w.source, *arena_);
        auto [tgt_node, tgt_port] = editor_math::path_to_node_port(w.target, *arena_);

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
    bp2::Direction fixed_direction;
    PortType fixed_type = PortType::Any;

    // Estimate a port's world position from blueprint layout data.
    // The exact pixel position depends on widget layout, but this is only
    // used as the reconnection anchor (visual wire preview start point),
    // so an approximate center-of-node position is sufficient.
    auto estimate_port_pos = [&](core::InternedId node_id, core::InternedId port_id) -> std::optional<Pt> {
        (void)port_id;
        const bp2::Blueprint::Node* node = host_->find_node(node_id);
        if (!node) return std::nullopt;
        constexpr float DEFAULT_W = 64.0f;
        constexpr float DEFAULT_H = 32.0f;
        float w = node->layout.width.value_or(DEFAULT_W);
        float h = node->layout.height.value_or(DEFAULT_H);
        return Pt(node->layout.x + w * 0.5f, node->layout.y + h * 0.5f);
    };

    if (detach_start) {
        fixed_direction = bp2::Direction::Input;
        auto [tgt_node, tgt_port] = editor_math::path_to_node_port(w.target, *arena_);
        fixed_type = resolve_effective_port_type(
            *host_,
            tgt_node,
            tgt_port,
            PortType::Any);
        if (!w.routing_points.empty()) {
            anchor_pos = Pt(w.routing_points.front().first, w.routing_points.front().second);
        } else if (auto pos = estimate_port_pos(tgt_node, tgt_port)) {
            anchor_pos = *pos;
        }
    } else {
        fixed_direction = bp2::Direction::Output;
        auto [src_node, src_port] = editor_math::path_to_node_port(w.source, *arena_);
        fixed_type = resolve_effective_port_type(
            *host_,
            src_node,
            src_port,
            PortType::Any);
        if (!w.routing_points.empty()) {
            anchor_pos = Pt(w.routing_points.back().first, w.routing_points.back().second);
        } else if (auto pos = estimate_port_pos(src_node, src_port)) {
            anchor_pos = *pos;
        }
    }
    return WirePortMatch{wire_index, detach_start, anchor_pos, fixed_direction, fixed_type};
}
