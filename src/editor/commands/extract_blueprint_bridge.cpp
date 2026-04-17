#include "extract_blueprint_internal.h"

#include "core/solvers/common/signal_key.h"

namespace editor::commands::extract_detail {

namespace {

std::string encode_port_type(PortType type) {
    switch (type) {
        case PortType::V: return "V";
        case PortType::I: return "I";
        case PortType::Bool: return "Bool";
        case PortType::RPM: return "RPM";
        case PortType::Temperature: return "Temperature";
        case PortType::Pressure: return "Pressure";
        case PortType::Position: return "Position";
        case PortType::Contextual: return "Contextual";
        case PortType::Any: return "Any";
    }
    return "Any";
}

} // namespace

bool create_bridge_nodes_for_side(
    bp2::Blueprint& out,
    const BridgeSideBuildParams& p,
    ui::StringInterner& interner,
    std::unordered_set<ui::InternedId>& used_node_ids,
    std::unordered_map<std::string, ui::InternedId>& out_bridge_ids,
    std::string* error_out) {
    (void)error_out;
    for (size_t i = 0; i < p.conns.size(); ++i) {
        const auto& ec = p.conns[i];
        const PortType port_type = resolve_port_type(ec);
        const Domain domain = editor::common::domain_for_port_type(port_type);

        bp2::Blueprint::Node bridge;
        bridge.semantic.type = interner.intern(p.is_input_side ? "BlueprintInput" : "BlueprintOutput");
        bridge.view.name = ec.iface_name;

        const std::string bridge_id = p.canonical_nested_instance_id
            ? signal_key::make_child_scope_key(interner.resolve(*p.canonical_nested_instance_id), ec.iface_name)
            : std::string(p.unique_prefix) + ec.iface_name;
        bridge.semantic.id = interner.intern(bridge_id);
        bridge.layout.x = p.x;

        auto y_it = p.node_center_y.find(ec.internal_node_id);
        bridge.layout.y = y_it != p.node_center_y.end()
            ? y_it->second + (static_cast<float>(i) * kMultiLaneOffsetY)
            : (p.fallback_y_origin + fallback_lane_y(i));

        if (p.is_input_side) {
            bridge.semantic.iface = bp2::Interface({
                bp2::PortDescriptor{interner.intern("ext"), domain, bp2::Direction::Input, port_type},
                bp2::PortDescriptor{interner.intern("port"), domain, bp2::Direction::Output, port_type},
            });
        } else {
            bridge.semantic.iface = bp2::Interface({
                bp2::PortDescriptor{interner.intern("port"), domain, bp2::Direction::Input, port_type},
                bp2::PortDescriptor{interner.intern("ext"), domain, bp2::Direction::Output, port_type},
            });
        }

        bridge.semantic.string_params["exposed_type"] = encode_port_type(port_type);
        out = out.with_node(std::move(bridge));
        used_node_ids.insert(interner.intern(bridge_id));
        out_bridge_ids[ec.iface_name] = interner.intern(bridge_id);
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
        auto it = bridge_ids.find(ec.iface_name);
        if (it == bridge_ids.end()) {
            continue;
        }

        bp2::Blueprint::Wire wire;
        wire.id = next_unique_id(interner, used_wire_ids, wire_prefix);
        used_wire_ids.insert(wire.id);
        wire.domain = ec.domain;

        const bp2::WireEndpoint bridge_ep{it->second, interner.intern("port")};
        const bp2::WireEndpoint internal_ep{ec.internal_node_id, ec.internal_port};

        if (is_input_side) {
            wire.source = bridge_ep;
            wire.target = internal_ep;
        } else {
            wire.source = internal_ep;
            wire.target = bridge_ep;
        }

        out = out.with_wire(std::move(wire));
    }
}

} // namespace editor::commands::extract_detail
