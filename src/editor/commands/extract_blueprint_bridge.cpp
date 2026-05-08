#include "extract_blueprint_internal.h"

#include "core/solvers/common/signal_key.h"

namespace editor::commands::extract_detail {

namespace {

std::string encode_port_type(PortType type) {
    switch (type) {
        case PortType::V: return "V";
        case PortType::I: return "I";
        case PortType::Signal: return "Signal";
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

void create_bridge_nodes_for_side(
    bp2::Blueprint& out,
    const BridgeSideBuildParams& p,
    core::StringInterner& interner,
    std::unordered_set<core::InternedId>& used_node_ids,
    std::unordered_map<std::string, core::InternedId>& out_bridge_ids) {
    for (size_t i = 0; i < p.conns.size(); ++i) {
        const auto& ec = p.conns[i];
        const PortType port_type = resolve_port_type(ec);
        const Domain domain = editor::common::domain_for_port_type(port_type);

        bp2::Blueprint::Node bridge;
        bridge.semantic.type = interner.intern("BridgePort");
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

        bridge.content = bp2::Blueprint::Node::BridgePortData{
            interner.intern(ec.iface_name),
            p.is_input_side
                ? bp2::BridgeDirection::Input
                : bp2::BridgeDirection::Output,
            port_type,
        };

        out = out.with_node(std::move(bridge));
        used_node_ids.insert(interner.intern(bridge_id));
        out_bridge_ids[ec.iface_name] = interner.intern(bridge_id);
    }
}

void append_bridge_to_internal_wires(
    bp2::Blueprint& out,
    const std::vector<ExternalConnection>& conns,
    bool is_input_side,
    const std::unordered_map<std::string, core::InternedId>& bridge_ids,
    const char* wire_prefix,
    core::StringInterner& interner,
    std::unordered_set<core::InternedId>& used_wire_ids) {
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

std::optional<SynthesizedBoundary> synthesize_extracted_boundary(
    const ExtractionPlan& plan,
    core::InternedId nested_instance_id,
    const std::vector<bp2::Blueprint::Node>& translated_nodes,
    core::StringInterner& interner,
    std::string* error_out) {
    SynthesizedBoundary boundary;
    boundary.child_interface = bp2::Interface(build_iface_ports(plan.inputs, plan.outputs, interner));

    bp2::Blueprint bridge_host;
    std::unordered_set<core::InternedId> used_node_ids;
    std::unordered_set<core::InternedId> used_wire_ids;
    std::unordered_map<std::string, core::InternedId> input_bridge_ids;
    std::unordered_map<std::string, core::InternedId> output_bridge_ids;

    const auto node_center_y = build_node_center_y_map(translated_nodes);
    float max_internal_right = 0.0f;
    for (const auto& node : translated_nodes) {
        max_internal_right = std::max(
            max_internal_right,
            node.layout.x + node.layout.width.value_or(kDefaultNodeWidth));
    }

    BridgeSideBuildParams const in_params{plan.inputs, true, node_center_y,
                                    0.0f, 0.0f, WindowScopeId::root(),
                                    "bp_in_", nullptr};
    create_bridge_nodes_for_side(bridge_host, in_params, interner,
                                 used_node_ids, input_bridge_ids);

    BridgeSideBuildParams const out_params{plan.outputs, false, node_center_y,
                                     max_internal_right + kBridgeMarginX, 0.0f,
                                     WindowScopeId::root(), "bp_out_", nullptr};
    create_bridge_nodes_for_side(bridge_host, out_params, interner,
                                 used_node_ids, output_bridge_ids);

    append_bridge_to_internal_wires(bridge_host, plan.inputs, true, input_bridge_ids,
                                    "bp_bridge_in_wire_", interner, used_wire_ids);
    append_bridge_to_internal_wires(bridge_host, plan.outputs, false, output_bridge_ids,
                                    "bp_bridge_out_wire_", interner, used_wire_ids);

    boundary.child_bridge_nodes = bridge_host.nodes();
    boundary.child_bridge_wires = bridge_host.wires();

    for (const auto& ec : plan.inputs) {
        bp2::Blueprint::Wire w;
        w.id = next_unique_id(interner, used_wire_ids, "extract_wire_");
        used_wire_ids.insert(w.id);
        w.domain = ec.domain;
        w.source = bp2::WireEndpoint{ec.external_node_id, ec.external_port};
        w.target = bp2::WireEndpoint{nested_instance_id, interner.intern(ec.iface_name)};
        boundary.parent_reconnection_wires.push_back(std::move(w));
    }
    for (const auto& ec : plan.outputs) {
        bp2::Blueprint::Wire w;
        w.id = next_unique_id(interner, used_wire_ids, "extract_wire_");
        used_wire_ids.insert(w.id);
        w.domain = ec.domain;
        w.source = bp2::WireEndpoint{nested_instance_id, interner.intern(ec.iface_name)};
        w.target = bp2::WireEndpoint{ec.external_node_id, ec.external_port};
        boundary.parent_reconnection_wires.push_back(std::move(w));
    }

    return boundary;
}

} // namespace editor::commands::extract_detail
