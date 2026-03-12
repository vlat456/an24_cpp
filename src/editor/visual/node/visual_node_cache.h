#pragma once

#include "editor/visual/node/node.h"
#include "editor/visual/node/types/bus_node.h"
#include "editor/visual/node/types/ref_node.h"
#include "editor/visual/node/types/group_node.h"
#include "editor/visual/node/types/text_node.h"
#include <unordered_map>
#include <memory>


class VisualNodeFactory {
public:
    static std::unique_ptr<VisualNode> create(const Node& node,
                                               const std::vector<Wire>& wires = {}) {
        if (node.render_hint == "bus") {
            return std::make_unique<BusVisualNode>(node, BusOrientation::Horizontal, wires);
        }
        if (node.render_hint == "ref") {
            return std::make_unique<RefVisualNode>(node);
        }
        if (node.render_hint == "group") {
            return std::make_unique<GroupVisualNode>(node);
        }
        if (node.render_hint == "text") {
            return std::make_unique<TextVisualNode>(node);
        }
        if (node.expandable) {
            Node bp_node = node;
            bp_node.node_content = NodeContent{};
            return std::make_unique<VisualNode>(bp_node);
        }
        return std::make_unique<VisualNode>(node);
    }
};

class VisualNodeCache {
public:
    VisualNodeCache() = default;

    VisualNode* getOrCreate(const Node& node, const std::vector<Wire>& wires = {});
    VisualNode* get(const std::string& node_id);
    void invalidate(const std::string& node_id) { cache_.erase(node_id); }
    void clear() { cache_.clear(); }

    void onWireAdded(const Wire& wire, const std::vector<Node>& all_nodes);
    void onWireDeleted(const Wire& wire, const std::vector<Node>& all_nodes);

private:
    std::unordered_map<std::string, std::unique_ptr<VisualNode>> cache_;
};

