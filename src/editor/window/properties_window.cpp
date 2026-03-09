#include "window/properties_window.h"

#ifndef EDITOR_TESTING
#include <imgui.h>
#endif

#include <algorithm>
#include <vector>
#include <sstream>

// Parse "k1:v1; k2:v2; ..." into parallel vectors
static bool parse_table_entries(const std::string& str,
                                std::vector<float>& keys,
                                std::vector<float>& values) {
    keys.clear();
    values.clear();
    size_t pos = 0;
    while (pos < str.size()) {
        while (pos < str.size() && (str[pos] == ' ' || str[pos] == ';')) ++pos;
        if (pos >= str.size()) break;
        size_t colon = str.find(':', pos);
        if (colon == std::string::npos) break;
        size_t end = str.find(';', colon + 1);
        if (end == std::string::npos) end = str.size();
        try {
            keys.push_back(std::stof(str.substr(pos, colon - pos)));
            values.push_back(std::stof(str.substr(colon + 1, end - colon - 1)));
        } catch (...) { break; }
        pos = end;
    }
    return !keys.empty();
}

// Serialize back to "k1:v1; k2:v2; ..."
static std::string serialize_table_entries(const std::vector<float>& keys,
                                           const std::vector<float>& values) {
    std::ostringstream oss;
    for (size_t i = 0; i < keys.size(); ++i) {
        if (i > 0) oss << "; ";
        oss << keys[i] << ":" << values[i];
    }
    return oss.str();
}

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
            if (key == "table") {
                renderTableParam(key);
                continue;
            }
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

void PropertiesWindow::renderTableParam(const std::string& key) {
#ifndef EDITOR_TESTING
    ImGui::Text("Lookup Table");

    std::vector<float> keys, values;
    parse_table_entries(target_->params[key], keys, values);

    bool changed = false;
    int remove_idx = -1;

    if (ImGui::BeginTable("##lut", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableSetupColumn("Input", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Output", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##del", ImGuiTableColumnFlags_WidthFixed, 24.0f);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < keys.size(); ++i) {
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(i));

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputFloat("##k", &keys[i], 0, 0, "%.2f")) changed = true;

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputFloat("##v", &values[i], 0, 0, "%.2f")) changed = true;

            ImGui::TableNextColumn();
            if (ImGui::SmallButton("X")) remove_idx = static_cast<int>(i);

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (remove_idx >= 0 && keys.size() > 1) {
        keys.erase(keys.begin() + remove_idx);
        values.erase(values.begin() + remove_idx);
        changed = true;
    }

    if (ImGui::Button("+ Add Row")) {
        float new_key = keys.empty() ? 0.0f : keys.back() + 1.0f;
        float new_val = keys.empty() ? 0.0f : values.back();
        keys.push_back(new_key);
        values.push_back(new_val);
        changed = true;
    }

    if (changed) {
        target_->params[key] = serialize_table_entries(keys, values);
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
