// Quick test to check if blueprint.json loads with auto-size
#include "editor/visual/scene/persist.h"
#include "editor/data/blueprint.h"
#include <iostream>

int main() {
    auto bp = load_blueprint("blueprint.json");

    std::cout << "Loaded " << bp.nodes.size() << " nodes\n";

    // Find a Voltmeter node
    for (const auto& n : bp.nodes) {
        if (n.type_name == "Voltmeter") {
            std::cout << "Voltmeter '" << n.name << "':\n";
            std::cout << "  size from JSON: " << n.size.x << "x" << n.size.y << "\n";
            std::cout << "  size_explicitly_set: " << (n.size_explicitly_set ? "true" : "false") << "\n";

            if (n.node_content.type == NodeContentType::Gauge) {
                std::cout << "  has Gauge content\n";
            }
        }
    }

    return 0;
}
