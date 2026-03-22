#include "inspector.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/path/path.h"
#include "ui/core/interned_id.h"
#include <algorithm>
#include <cctype>

Inspector::Inspector(const bp2::Blueprint* bp, const bp2::PathArena* arena,
                     const ui::StringInterner* interner, const std::string& group_id)
    : bp_(bp), arena_(arena), interner_(interner), group_id_(group_id) {}

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
    size_t nc = bp_->nodes().size();
    size_t wc = bp_->wires().size();
    if (nc != last_node_count_ || wc != last_wire_count_) {
        last_node_count_ = nc;
        last_wire_count_ = wc;
        dirty_ = true;
    }
    return dirty_;
}

std::pair<ui::InternedId, ui::InternedId> Inspector::decode_port_path(bp2::Path p) const {
    // Expects: Port -> Node -> Root
    if (!arena_) return {};
    if (p.kind() != bp2::PathKind::Port) return {};
    ui::InternedId port_name = p.segment();
    bp2::Path node_path = arena_->parent(p);
    if (node_path.kind() != bp2::PathKind::Node) return {};
    ui::InternedId node_id = node_path.segment();
    return {node_id, port_name};
}

bool Inspector::ownsWire(const bp2::Blueprint::Wire& w) const {
    if (!bp_) return false;
    auto [src_node, src_port] = decode_port_path(w.source);
    auto [tgt_node, tgt_port] = decode_port_path(w.target);
    if (src_node.empty() || tgt_node.empty()) return false;
    const auto* n1 = bp_->find_node(src_node);
    const auto* n2 = bp_->find_node(tgt_node);
    return n1 && n2 && n1->group_id == group_id_ && n2->group_id == group_id_;
}

void Inspector::buildDisplayTree() {
    display_tree_.clear();
    if (!bp_ || !arena_ || !interner_) return;

    for (const auto& node : bp_->nodes()) {
        if (!ownsNode(node)) continue;
        if (!passesFilter(node)) continue;

        DisplayNode dn;
        dn.node_id = std::string(interner_->resolve(node.id));
        dn.name = node.name;
        // bp2::Blueprint::Node stores the type as an InternedId — resolve it
        dn.type_name = std::string(interner_->resolve(node.type));

        // Count connections (only wires owned by this group)
        size_t conn_count = 0;
        for (const auto& wire : bp_->wires()) {
            if (!ownsWire(wire)) continue;
            auto [src_node, src_port] = decode_port_path(wire.source);
            auto [tgt_node, tgt_port] = decode_port_path(wire.target);
            if (src_node == node.id || tgt_node == node.id)
                conn_count++;
        }
        dn.connection_count = conn_count;

        // Collect ports (inputs then outputs)
        for (const auto& port : node.inputs) {
            DisplayPort dp;
            dp.name = std::string(interner_->resolve(port.name));
            dp.side = PortSide::Input;
            dp.connection = findConnectionFor(node, port, PortSide::Input);
            dn.ports.push_back(std::move(dp));
        }
        for (const auto& port : node.outputs) {
            DisplayPort dp;
            dp.name = std::string(interner_->resolve(port.name));
            dp.side = PortSide::Output;
            dp.connection = findConnectionFor(node, port, PortSide::Output);
            dn.ports.push_back(std::move(dp));
        }

        display_tree_.push_back(std::move(dn));
    }

    sortDisplayTree();
}

std::string Inspector::findConnectionFor(const bp2::Blueprint::Node& node,
                                          const EditorPort& port, PortSide side) const {
    std::string result;

    for (const auto& wire : bp_->wires()) {
        if (!ownsWire(wire)) continue;

        auto [src_node, src_port] = decode_port_path(wire.source);
        auto [tgt_node, tgt_port] = decode_port_path(wire.target);

        // Match the port's side: inputs match wire.target, outputs match wire.source
        ui::InternedId local_node  = (side == PortSide::Input) ? tgt_node : src_node;
        ui::InternedId local_port  = (side == PortSide::Input) ? tgt_port : src_port;
        ui::InternedId remote_node = (side == PortSide::Input) ? src_node : tgt_node;
        ui::InternedId remote_port = (side == PortSide::Input) ? src_port : tgt_port;

        if (local_node == node.id && local_port == port.name) {
            const auto* other = bp_->find_node(remote_node);
            if (other) {
                if (!result.empty()) result += ", ";
                result += other->name + "." + std::string(interner_->resolve(remote_port));
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

    std::string type_str = std::string(interner_->resolve(node.type));
    return contains_lower(node.name) || contains_lower(type_str);
}
