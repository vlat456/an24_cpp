#include "inspector.h"
#include "visual/scene/scene.h"
#include "data/node.h"
#include "data/port.h"
#include <algorithm>
#include <cctype>
#include <cstring>

Inspector::Inspector(VisualScene& scene) : scene_(scene) {
    search_buffer_[0] = '\0';
    dirty_ = true;
}

void Inspector::setSearch(const char* search) {
    if (std::strcmp(search_buffer_, search) != 0) {
        std::strncpy(search_buffer_, search, sizeof(search_buffer_) - 1);
        search_buffer_[sizeof(search_buffer_) - 1] = '\0';
        dirty_ = true;
    }
}

void Inspector::buildDisplayTree() {
    display_tree_.clear();

    for (const auto& node : scene_.nodes()) {
        if (!passesFilter(node)) continue;

        DisplayNode dn;
        dn.name = node.name;
        dn.type_name = node.type_name;

        // Count connections for this node
        size_t conn_count = 0;
        for (const auto& wire : scene_.wires()) {
            if (wire.start.node_id == node.id || wire.end.node_id == node.id) {
                conn_count++;
            }
        }
        dn.connection_count = conn_count;

        // Add ports
        for (const auto& port : node.inputs) {
            DisplayPort dp;
            dp.name = port.name;
            dp.side = PortSide::Input;
            dp.connection = findConnectionFor(node, port, PortSide::Input);
            dn.ports.push_back(dp);
        }

        for (const auto& port : node.outputs) {
            DisplayPort dp;
            dp.name = port.name;
            dp.side = PortSide::Output;
            dp.connection = findConnectionFor(node, port, PortSide::Output);
            dn.ports.push_back(dp);
        }

        display_tree_.push_back(dn);
    }

    // Sort by selected mode
    sortDisplayTree();
}

std::string Inspector::findConnectionFor(const Node& node, const Port& port, PortSide side) {
    for (const auto& wire : scene_.wires()) {
        if (side == PortSide::Input) {
            if (wire.end.node_id == node.id && wire.end.port_name == port.name) {
                const Node* other = scene_.findNode(wire.start.node_id.c_str());
                if (other) {
                    return other->name + "." + wire.start.port_name;
                }
            }
        } else {
            if (wire.start.node_id == node.id && wire.start.port_name == port.name) {
                const Node* other = scene_.findNode(wire.end.node_id.c_str());
                if (other) {
                    return other->name + "." + wire.end.port_name;
                }
            }
        }
    }
    return "[not connected]";
}

void Inspector::sortDisplayTree() {
    auto cmp = [this](const DisplayNode& a, const DisplayNode& b) {
        switch (sort_mode_) {
            case SortMode::Type:
                return a.type_name < b.type_name;
            case SortMode::Connections:
                return a.connection_count > b.connection_count;
            case SortMode::Name:
            default:
                return a.name < b.name;
        }
    };
    std::sort(display_tree_.begin(), display_tree_.end(), cmp);
}

bool Inspector::passesFilter(const Node& node) const {
    if (search_buffer_[0] == '\0') return true;

    std::string search_lower = search_buffer_;
    std::transform(search_lower.begin(), search_lower.end(), search_lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    std::string name_lower = node.name;
    std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    std::string type_lower = node.type_name;
    std::transform(type_lower.begin(), type_lower.end(), type_lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    return name_lower.find(search_lower) != std::string::npos ||
           type_lower.find(search_lower) != std::string::npos;
}
