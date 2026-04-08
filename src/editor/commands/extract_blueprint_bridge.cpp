#include "extract_blueprint_internal.h"

#include <algorithm>

namespace editor::commands::extract_detail {

bool create_bridge_nodes_for_side(
    bp2::Blueprint& out,
    const BridgeSideBuildParams& p,
    ui::StringInterner& interner,
    std::unordered_set<ui::InternedId>& used_node_ids,
    std::unordered_map<std::string, ui::InternedId>& out_bridge_ids,
    std::string* error_out) {
    std::vector<size_t> order(p.conns.size());
    for (size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        float ay = p.fallback_y_origin + fallback_lane_y(a);
        float by = p.fallback_y_origin + fallback_lane_y(b);
        if (auto it = p.node_center_y.find(p.conns[a].internal_node_id); it != p.node_center_y.end()) {
            ay = it->second;
        }
        if (auto it = p.node_center_y.find(p.conns[b].internal_node_id); it != p.node_center_y.end()) {
            by = it->second;
        }
        const float epsilon = 0.5f;
        if (ay < by - epsilon) {
            return true;
        }
        if (ay > by + epsilon) {
            return false;
        }
        return p.conns[a].iface_name < p.conns[b].iface_name;
    });

    std::unordered_map<ui::InternedId, int> lane_counts;
    for (size_t rank = 0; rank < order.size(); ++rank) {
        const auto& ec = p.conns[order[rank]];

        ui::InternedId id;
        if (p.canonical_nested_instance_id) {
            id = make_iface_bridge_id(interner, *p.canonical_nested_instance_id, ec.iface_name);
            if (used_node_ids.find(id) != used_node_ids.end()) {
                return set_error(error_out, "extract bridge node id collision");
            }
        } else {
            id = next_unique_id(interner, used_node_ids, p.unique_prefix);
        }
        used_node_ids.insert(id);
        out_bridge_ids[ec.iface_name] = id;

         bp2::Blueprint::Node n;
         n.semantic.id = id;
         n.semantic.type = interner.intern(p.is_input_side ? "BlueprintInput" : "BlueprintOutput");
         n.view.name = ec.iface_name;
         n.layout.group_id = p.group_id;
         n.layout.x = p.x;

         float base_y = p.fallback_y_origin + fallback_lane_y(rank);
         if (auto it = p.node_center_y.find(ec.internal_node_id); it != p.node_center_y.end()) {
             const int lane = lane_counts[ec.internal_node_id]++;
             base_y = it->second + static_cast<float>(lane) * kMultiLaneOffsetY;
         }
         n.layout.y = base_y;

        const PortType pt = resolve_port_type(ec);
         const Domain pd = editor::common::domain_for_port_type(pt);
         if (p.is_input_side) {
             n.view.inputs.emplace_back(interner.intern("ext"), bp2::PortSide::Input, pt);
             n.view.outputs.emplace_back(interner.intern("port"), bp2::PortSide::Output, pt);
             bp2::PortDescriptor ext_pd;
             ext_pd.name = interner.intern("ext");
             ext_pd.domain = pd;
             ext_pd.direction = bp2::Direction::Input;
             bp2::PortDescriptor port_pd;
             port_pd.name = interner.intern("port");
             port_pd.domain = pd;
             port_pd.direction = bp2::Direction::Output;
             n.semantic.iface = bp2::Interface({ext_pd, port_pd});
         } else {
             n.view.inputs.emplace_back(interner.intern("port"), bp2::PortSide::Input, pt);
             n.view.outputs.emplace_back(interner.intern("ext"), bp2::PortSide::Output, pt);
             bp2::PortDescriptor ext_pd;
             ext_pd.name = interner.intern("ext");
             ext_pd.domain = pd;
             ext_pd.direction = bp2::Direction::Output;
             bp2::PortDescriptor port_pd;
             port_pd.name = interner.intern("port");
             port_pd.domain = pd;
             port_pd.direction = bp2::Direction::Input;
             n.semantic.iface = bp2::Interface({ext_pd, port_pd});
         }
        out = out.with_node(std::move(n));
    }
    return true;
}

void append_bridge_to_internal_wires(
    bp2::Blueprint& out,
    const std::vector<ExternalConnection>& conns,
    bool is_input_side,
    const std::unordered_map<std::string, ui::InternedId>& bridge_ids,
    const char* wire_prefix,
    ui::StringInterner& interner,
    bp2::PathArena& arena,
    std::unordered_set<ui::InternedId>& used_wire_ids) {
    for (const auto& ec : conns) {
        bp2::Blueprint::Wire w;
        w.id = next_unique_id(interner, used_wire_ids, wire_prefix);
        used_wire_ids.insert(w.id);
        w.domain = ec.domain;
        if (is_input_side) {
            w.source = arena.make_port(arena.make_node(arena.root(), bridge_ids.at(ec.iface_name)), interner.intern("port"));
            w.target = arena.make_port(arena.make_node(arena.root(), ec.internal_node_id), ec.internal_port);
        } else {
            w.source = arena.make_port(arena.make_node(arena.root(), ec.internal_node_id), ec.internal_port);
            w.target = arena.make_port(arena.make_node(arena.root(), bridge_ids.at(ec.iface_name)), interner.intern("port"));
        }
        out = out.with_wire(std::move(w));
    }
}

} // namespace editor::commands::extract_detail
