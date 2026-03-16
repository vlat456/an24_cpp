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
            auto sz = n.get_size();
            std::cout << "  size: " << sz.x << "x" << sz.y << "\n";
            std::cout << "  has_explicit_size: " << (n.has_explicit_size() ? "true" : "false") << "\n";

            if (n.node_content.type == NodeContentType::Gauge) {
                std::cout << "  has Gauge content\n";
            }
        }
    }

    return 0;
}
