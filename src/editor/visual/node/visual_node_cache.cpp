#include "visual_node_cache.h"
#include "editor/visual/node/node_utils.h"


VisualNode* VisualNodeCache::getOrCreate(const Node& node, const std::vector<Wire>& wires) {
    auto it = cache_.find(node.id);
    if (it == cache_.end()) {
        auto visual = VisualNodeFactory::create(node, wires);
        auto* ptr = visual.get();
        cache_[node.id] = std::move(visual);
        return ptr;
    }

    if (it->second->getContentType() != node.node_content.type) {
        auto visual = VisualNodeFactory::create(node, wires);
        auto* ptr = visual.get();
        cache_[node.id] = std::move(visual);
        return ptr;
    }

    it->second->updateNodeContent(node.node_content);
    it->second->setPosition(node_utils::snap_to_grid(node.pos));
    if (it->second->isResizable())
        it->second->setSize(node.size);
    return it->second.get();
}

VisualNode* VisualNodeCache::get(const std::string& node_id) {
    auto it = cache_.find(node_id);
    return it != cache_.end() ? it->second.get() : nullptr;
}

void VisualNodeCache::onWireAdded(const Wire& wire, const std::vector<Node>& all_nodes) {
    for (const auto& node : all_nodes) {
        if (node.id == wire.start.node_id || node.id == wire.end.node_id) {
            auto* visual = get(node.id);
            if (visual) {
                visual->connectWire(wire);
            }
        }
    }
}

void VisualNodeCache::onWireDeleted(const Wire& wire, const std::vector<Node>& all_nodes) {
    for (const auto& node : all_nodes) {
        if (node.id == wire.start.node_id || node.id == wire.end.node_id) {
            auto* visual = get(node.id);
            if (visual) {
                visual->disconnectWire(wire);
            }
        }
    }
}

