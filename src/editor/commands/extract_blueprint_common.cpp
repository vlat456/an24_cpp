#include "extract_blueprint_internal.h"

#include <algorithm>
#include <tuple>

namespace editor::commands::extract_detail {

PortType find_port_type(const bp2::Blueprint& bp,
                        const bp2::Blueprint::Node* node,
                        ui::InternedId port_name) {
    if (!node) {
        return PortType::Any;
    }
    for (const auto& p : bp.effective_node_iface(*node).ports()) {
        if (p.name == port_name) {
            return p.port_type;
        }
    }
    return PortType::Any;
}

bool path_to_node_port(const bp2::Path& path,
                       const bp2::PathArena& arena,
                       ui::InternedId& out_node,
                       ui::InternedId& out_port) {
    if (path.kind() != bp2::PathKind::Port) {
        return false;
    }
    const bp2::Path parent = arena.parent(path);
    if (parent.kind() != bp2::PathKind::Node) {
        return false;
    }
    out_node = parent.segment();
    out_port = path.segment();
    return !out_node.empty() && !out_port.empty();
}

bool path_to_node_port(const bp2::WireEndpoint& ep,
                       const bp2::PathArena& /*arena*/,
                       ui::InternedId& out_node,
                       ui::InternedId& out_port) {
    out_node = ep.node;
    out_port = ep.port;
    return !out_node.empty() && !out_port.empty();
}

std::string dedupe_name(const std::string& base,
                        std::unordered_set<std::string>& used) {
    if (used.find(base) == used.end()) {
        used.insert(base);
        return base;
    }
    for (int i = 2; i < 1000000; ++i) {
        std::string candidate = base + "_" + std::to_string(i);
        if (used.find(candidate) == used.end()) {
            used.insert(candidate);
            return candidate;
        }
    }
    return base + "_overflow";
}

ui::InternedId next_unique_id(ui::StringInterner& interner,
                              const std::unordered_set<ui::InternedId>& used,
                              const std::string& prefix) {
    for (int i = 1; i < 1000000; ++i) {
        std::string candidate = prefix + std::to_string(i);
        ui::InternedId id = interner.lookup(candidate);
        if (id.empty() || used.find(id) == used.end()) {
            return interner.intern(candidate);
        }
    }
    return {};
}

std::unordered_set<ui::InternedId> collect_used_node_ids(const bp2::Blueprint& bp) {
    std::unordered_set<ui::InternedId> out;
    out.reserve(bp.nodes().size());
    for (const auto& n : bp.nodes()) {
        out.insert(n.semantic.id);
    }
    return out;
}

std::unordered_set<ui::InternedId> collect_used_wire_ids(const bp2::Blueprint& bp) {
    std::unordered_set<ui::InternedId> out;
    out.reserve(bp.wires().size());
    for (const auto& w : bp.wires()) {
        out.insert(w.id);
    }
    return out;
}

bool compare_external(const ExternalConnection& a, const ExternalConnection& b) {
    return std::make_tuple(a.original_wire_id.raw(),
                           a.external_node_id.raw(), a.external_port.raw(),
                           a.internal_node_id.raw(), a.internal_port.raw())
        < std::make_tuple(b.original_wire_id.raw(),
                          b.external_node_id.raw(), b.external_port.raw(),
                          b.internal_node_id.raw(), b.internal_port.raw());
}

std::unordered_map<ui::InternedId, float> build_node_center_y_map(
    const std::vector<bp2::Blueprint::Node>& nodes) {
    std::unordered_map<ui::InternedId, float> out;
    out.reserve(nodes.size());
    for (const auto& n : nodes) {
        const float h = n.layout.height.value_or(kDefaultNodeHeight);
        out[n.semantic.id] = n.layout.y + (h * 0.5f);
    }
    return out;
}

float fallback_lane_y(size_t index) {
    return kFallbackLaneStartY + static_cast<float>(index) * kFallbackLaneStepY;
}

ui::InternedId make_iface_bridge_id(ui::StringInterner& interner,
                                    ui::InternedId nested_instance_id,
                                    const std::string& iface_name) {
    std::string id = std::string(interner.resolve(nested_instance_id));
    id += ":";
    id += iface_name;
    return interner.intern(id);
}

void dedupe_iface_names(std::vector<ExternalConnection>& conns,
                        const char* default_name) {
    std::unordered_set<std::string> used;
    for (auto& ec : conns) {
        if (ec.iface_name.empty()) {
            ec.iface_name = default_name;
        }
        ec.iface_name = dedupe_name(ec.iface_name, used);
    }
}

} // namespace editor::commands::extract_detail
