#include "inspector.h"
#include <algorithm>
#include <cctype>

Inspector::Inspector(const VisualScene* scene) : scene_(scene) {}

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
    if (!scene_) return false;
    size_t nc = scene_->nodes().size();
    size_t wc = scene_->wires().size();
    if (nc != last_node_count_ || wc != last_wire_count_) {
        last_node_count_ = nc;
        last_wire_count_ = wc;
        dirty_ = true;
    }
    return dirty_;
}

void Inspector::buildDisplayTree() {
    display_tree_.clear();
    if (!scene_) return;

    for (const auto& node : scene_->nodes()) {
        if (!scene_->ownsNode(node)) continue;
        if (!passesFilter(node)) continue;

        DisplayNode dn;
        dn.node_id = node.id;
        dn.name = node.name;
        dn.type_name = node.type_name;

        // Count connections (only wires owned by this scene)
        size_t conn_count = 0;
        for (const auto& wire : scene_->wires()) {
            if (!scene_->ownsWire(wire)) continue;
            if (wire.start.node_id == node.id || wire.end.node_id == node.id)
                conn_count++;
        }
        dn.connection_count = conn_count;

        // Collect ports (inputs then outputs)
        for (const auto& port : node.inputs) {
            dn.ports.push_back({port.name, PortSide::Input,
                                findConnectionFor(node, port, PortSide::Input)});
        }
        for (const auto& port : node.outputs) {
            dn.ports.push_back({port.name, PortSide::Output,
                                findConnectionFor(node, port, PortSide::Output)});
        }

        display_tree_.push_back(std::move(dn));
    }

    sortDisplayTree();
}

std::string Inspector::findConnectionFor(const Node& node, const EditorPort& port, PortSide side) const {
    std::string result;
    for (const auto& wire : scene_->wires()) {
        if (!scene_->ownsWire(wire)) continue;

        // Match the port's side: inputs match wire.end, outputs match wire.start
        const WireEnd& local = (side == PortSide::Input) ? wire.end : wire.start;
        const WireEnd& remote = (side == PortSide::Input) ? wire.start : wire.end;

        if (local.node_id == node.id && local.port_name == port.name) {
            const Node* other = scene_->findNode(remote.node_id.c_str());
            if (other) {
                if (!result.empty()) result += ", ";
                result += other->name + "." + remote.port_name;
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

bool Inspector::passesFilter(const Node& node) const {
    if (search_lower_.empty()) return true;

    // Match against name or type_name (case-insensitive)
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

    return contains_lower(node.name) || contains_lower(node.type_name);
}
