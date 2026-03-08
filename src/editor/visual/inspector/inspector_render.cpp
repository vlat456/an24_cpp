#include "inspector.h"
#include <imgui.h>

void Inspector::render() {
    // Lazy rebuild: check scene topology + dirty flag
    if (detectSceneChange() || dirty_) {
        buildDisplayTree();
        dirty_ = false;
    }

    ImGui::Text("Component Tree (%zu nodes)", display_tree_.size());

    // Search bar (two-way bind with search_ string)
    static char buf[256];
    if (search_.size() < sizeof(buf)) {
        std::copy(search_.begin(), search_.end(), buf);
        buf[search_.size()] = '\0';
    }
    if (ImGui::InputText("Search", buf, sizeof(buf))) {
        setSearch(buf);
    }

    // Sort mode buttons
    ImGui::SameLine();
    if (ImGui::SmallButton("Name")) setSortMode(SortMode::Name);
    ImGui::SameLine();
    if (ImGui::SmallButton("Type")) setSortMode(SortMode::Type);
    ImGui::SameLine();
    if (ImGui::SmallButton("Conn")) setSortMode(SortMode::Connections);

    ImGui::Separator();

    // Node tree
    for (const auto& node : display_tree_) {
        std::string label = node.name + " [" + node.type_name + "] (" +
                            std::to_string(node.connection_count) + " conn)";

        if (ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            for (const auto& port : node.ports) {
                const char* arrow = (port.side == PortSide::Input) ? "\xe2\x86\x90" : "\xe2\x86\x92"; // ← / →
                bool connected = (port.connection != "[not connected]");
                if (connected) {
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f),
                                       "%s %s %s", port.name.c_str(), arrow,
                                       port.connection.c_str());
                } else {
                    ImGui::BulletText("%s %s", port.name.c_str(), arrow);
                }
            }
            ImGui::TreePop();
        }
    }

    if (display_tree_.empty()) {
        ImGui::TextDisabled("No components (search filtered all)");
    }
}
