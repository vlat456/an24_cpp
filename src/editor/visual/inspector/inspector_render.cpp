#include "inspector.h"
#include <imgui.h>
#include <cstring>

void Inspector::render() {
    // Check if scene changed (nodes/wires added or removed)
    size_t current_node_count = scene_.nodes().size();
    size_t current_wire_count = scene_.wires().size();
    bool scene_changed = (current_node_count != last_node_count_ ||
                          current_wire_count != last_wire_count_);
    if (scene_changed) {
        last_node_count_ = current_node_count;
        last_wire_count_ = current_wire_count;
        dirty_ = true;
    }

    // Only rebuild display tree when dirty (scene changed, search changed)
    if (dirty_) {
        buildDisplayTree();
        dirty_ = false;
    }

    ImGui::Text("Component Tree (%zu nodes)", display_tree_.size());

    // Search bar
    char buf[128];
    std::strncpy(buf, search_buffer_, sizeof(buf));
    buf[sizeof(buf) - 1] = '\0';
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

    // Render each node with its ports and connections
    for (const auto& node : display_tree_) {
        std::string label = node.name + " [" + node.type_name + "] (" +
                            std::to_string(node.connection_count) + " conn)";
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen;

        if (ImGui::TreeNodeEx(label.c_str(), flags)) {
            // Input ports
            for (const auto& port : node.ports) {
                if (port.side != PortSide::Input) continue;

                const char* arrow = "←";
                if (port.connection == "[not connected]") {
                    ImGui::BulletText("%s %s", port.name.c_str(), arrow);
                } else {
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f),
                                      "%s %s %s", port.name.c_str(), arrow,
                                      port.connection.c_str());
                }
            }

            // Output ports
            for (const auto& port : node.ports) {
                if (port.side != PortSide::Output) continue;

                const char* arrow = "→";
                if (port.connection == "[not connected]") {
                    ImGui::BulletText("%s %s", port.name.c_str(), arrow);
                } else {
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f),
                                      "%s %s %s", port.name.c_str(), arrow,
                                      port.connection.c_str());
                }
            }

            ImGui::TreePop();
        }
    }

    if (display_tree_.empty()) {
        ImGui::TextDisabled("No components (search filtered all)");
    }
}
