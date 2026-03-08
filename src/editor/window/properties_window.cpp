#include "window/properties_window.h"

#ifndef EDITOR_TESTING
#include <imgui.h>
#endif

#include <algorithm>
#include <vector>

void PropertiesWindow::open(Node& node, PropertyCallback on_apply) {
    target_ = &node;
    target_node_id_ = node.id;
    on_apply_ = std::move(on_apply);

    // Snapshot for cancel/revert
    snapshot_name_ = node.name;
    snapshot_params_ = node.params;

    open_ = true;
}

void PropertiesWindow::close() {
    cancelAndClose();
}

void PropertiesWindow::render() {
    if (!open_ || !target_) return;

#ifndef EDITOR_TESTING
    ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_FirstUseEver);
    bool window_open = true;
    if (ImGui::Begin(("Properties: " + target_node_id_).c_str(), &window_open)) {
        // Header
        ImGui::Text("%s (%s)", target_node_id_.c_str(), target_->type_name.c_str());
        ImGui::Separator();

        // Name field
        char name_buf[256];
        strncpy(name_buf, target_->name.c_str(), sizeof(name_buf) - 1);
        name_buf[sizeof(name_buf) - 1] = '\0';
        if (ImGui::InputText("Name", name_buf, sizeof(name_buf))) {
            target_->name = name_buf;
        }

        ImGui::Separator();
        ImGui::Text("Parameters");
        ImGui::Separator();

        // Sort param keys for stable ordering
        std::vector<std::string> keys;
        keys.reserve(target_->params.size());
        for (const auto& [k, _] : target_->params) keys.push_back(k);
        std::sort(keys.begin(), keys.end());

        // Param fields
        for (const auto& key : keys) {
            char buf[256];
            strncpy(buf, target_->params[key].c_str(), sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            if (ImGui::InputText(key.c_str(), buf, sizeof(buf))) {
                target_->params[key] = buf;
            }
        }

        ImGui::Separator();

        // OK / Cancel buttons
        if (ImGui::Button("OK", ImVec2(120, 0))) {
            applyAndClose();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            cancelAndClose();
        }
    }
    ImGui::End();

    // Window closed via X button
    if (!window_open) {
        cancelAndClose();
    }
#endif
}

void PropertiesWindow::applyAndClose() {
    if (target_ && on_apply_) {
        on_apply_(target_node_id_);
    }
    open_ = false;
    target_ = nullptr;
}

void PropertiesWindow::cancelAndClose() {
    if (target_) {
        // Restore snapshot
        target_->name = snapshot_name_;
        target_->params = snapshot_params_;
    }
    open_ = false;
    target_ = nullptr;
}
