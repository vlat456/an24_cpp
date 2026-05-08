#include "inspector.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/interface/node_port_projection.h"
#include "blueprint_v2/path/path.h"
#include "core/strings/interned_id.h"
#include "window/window_scope_id.h"
#include <algorithm>
#include <cctype>

Inspector::Inspector(const bp2::Blueprint* bp, const bp2::PathArena* arena,
                     core::StringInterner* interner, const WindowScopeId& scope_id,
                     const ComponentRegistry* registry)
    : bp_(bp), arena_(arena), interner_(interner), registry_(registry), scope_id_(scope_id) {}

std::string Inspector::consumeSelection() {
    std::string result;
    result.swap(clicked_node_id_);
    return result;
}

void Inspector::setSearch(std::string_view search) {
    if (search_ == search) return;
    search_ = search;
    // Precompute lowercase to avoid per-node allocation in passesFilter
    search_lower_.resize(search_.size());
    std::transform(search_.begin(), search_.end(), search_lower_.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    dirty_ = true;
}

void Inspector::setSortMode(SortMode mode) {
    if (sort_mode_ != mode) {
        sort_mode_ = mode;
        dirty_ = true;
    }
}

bool Inspector::detectSceneChange() {
    if (!bp_) return false;
    size_t const nc = bp_->nodes().size();
    size_t const wc = bp_->wires().size();
    if (nc != last_node_count_ || wc != last_wire_count_) {
        last_node_count_ = nc;
        last_wire_count_ = wc;
        dirty_ = true;
    }
    return dirty_;
}

std::pair<core::InternedId, core::InternedId> Inspector::decode_port_path(bp2::Path p) const {
    // Expects: Port -> Node -> Root
    if (!arena_) return {};
    if (p.kind() != bp2::PathKind::Port) return {};
    core::InternedId const port_name = p.segment();
    bp2::Path const node_path = arena_->parent(p);
    if (node_path.kind() != bp2::PathKind::Node) return {};
    core::InternedId const node_id = node_path.segment();
    return {node_id, port_name};
}

std::pair<core::InternedId, core::InternedId> Inspector::decode_port_path(bp2::WireEndpoint const& ep) const {
    return {ep.node, ep.port};
}

bool Inspector::ownsWire(const bp2::Blueprint::Wire& w) const {
    if (!bp_) return false;
    auto [src_node, src_port] = decode_port_path(w.source);
    auto [tgt_node, tgt_port] = decode_port_path(w.target);
    if (src_node.empty() || tgt_node.empty()) return false;
    const auto* n1 = bp_->find_node(src_node);
    const auto* n2 = bp_->find_node(tgt_node);
    return n1 && n2;
}

void Inspector::buildDisplayTree() {
    display_tree_.clear();
    if (!bp_ || !arena_ || !interner_) return;

     // Pre-decode owned wires once (avoids O(N*P*W) redundant decode_port_path calls).
     std::vector<DecodedWire> owned_wires;
     for (const auto& wire : bp_->wires()) {
         auto [sn, sp] = decode_port_path(wire.source);
         auto [tn, tp] = decode_port_path(wire.target);
         if (sn.empty() || tn.empty()) continue;
         const auto* n1 = bp_->find_node(sn);
         const auto* n2 = bp_->find_node(tn);
         if (!n1 || !n2) continue;
         owned_wires.push_back({sn, sp, tn, tp});
     }

    for (const auto& node : bp_->nodes()) {
        if (!ownsNode(node)) continue;
        if (!passesFilter(node)) continue;

        DisplayNode dn;
        dn.node_id = std::string(interner_->resolve(node.semantic.id));
        dn.name = node.view.name;
        dn.type_name = std::string(interner_->resolve(node.semantic.type));

        // Count connections from pre-decoded wires
        size_t conn_count = 0;
        for (const auto& dw : owned_wires) {
            if (dw.src_node == node.semantic.id || dw.tgt_node == node.semantic.id)
                conn_count++;
        }
        dn.connection_count = conn_count;

        // Collect ports (inputs then outputs)
        const bp2::Interface iface = bp_->resolve_node_iface(
            node,
            bp2::Blueprint::NodeIfaceAuthority{*interner_, registry_});
        const auto inputs = bp2::derive_input_ports(iface);
        const auto outputs = bp2::derive_output_ports(iface);
        for (const auto& port : inputs) {
            DisplayPort dp;
            dp.name = std::string(interner_->resolve(port.name));
            dp.direction = bp2::Direction::Input;
            dp.connection = findConnectionFor(node, port, bp2::Direction::Input, owned_wires);
            dn.ports.push_back(std::move(dp));
        }
        for (const auto& port : outputs) {
            DisplayPort dp;
            dp.name = std::string(interner_->resolve(port.name));
            dp.direction = bp2::Direction::Output;
            dp.connection = findConnectionFor(node, port, bp2::Direction::Output, owned_wires);
            dn.ports.push_back(std::move(dp));
        }

        display_tree_.push_back(std::move(dn));
    }

    sortDisplayTree();
}

std::string Inspector::findConnectionFor(const bp2::Blueprint::Node& node,
                                          const bp2::PortDescriptor& port, bp2::Direction direction,
                                          const std::vector<DecodedWire>& wires) const {
    std::string result;

    for (const auto& dw : wires) {
        // Match the port's direction: inputs match wire.target, outputs match wire.source
        core::InternedId const local_node  = (direction == bp2::Direction::Input) ? dw.tgt_node : dw.src_node;
        core::InternedId const local_port  = (direction == bp2::Direction::Input) ? dw.tgt_port : dw.src_port;
        core::InternedId const remote_node = (direction == bp2::Direction::Input) ? dw.src_node : dw.tgt_node;
        core::InternedId const remote_port = (direction == bp2::Direction::Input) ? dw.src_port : dw.tgt_port;

        if (local_node == node.semantic.id && local_port == port.name) {
            const auto* other = bp_->find_node(remote_node);
            if (other) {
                if (!result.empty()) result += ", ";
                result += other->view.name + "." + std::string(interner_->resolve(remote_port));
            }
        }
    }
    return result.empty() ? "[not connected]" : result;
}

void Inspector::sortDisplayTree() {
    auto cmp = [this](const DisplayNode& a, const DisplayNode& b) {
        switch (sort_mode_) {
            case SortMode::Type:        return a.type_name < b.type_name;
            case SortMode::Connections: return a.connection_count > b.connection_count;
            case SortMode::Name:
            default:                    return a.name < b.name;
        }
    };
    std::sort(display_tree_.begin(), display_tree_.end(), cmp);
}

bool Inspector::passesFilter(const bp2::Blueprint::Node& node) const {
    if (search_lower_.empty()) return true;

    // Match against name or type (case-insensitive)
    auto contains_lower = [&](const std::string& haystack) {
        if (haystack.size() < search_lower_.size()) return false;
        return std::search(
            haystack.begin(), haystack.end(),
            search_lower_.begin(), search_lower_.end(),
            [](unsigned char a, unsigned char b) {
                return std::tolower(a) == b;  // b is already lowercase
            }
        ) != haystack.end();
    };

    std::string const type_str = std::string(interner_->resolve(node.semantic.type));
    return contains_lower(node.view.name) || contains_lower(type_str);
}
