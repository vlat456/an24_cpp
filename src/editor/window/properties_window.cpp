#include "window/properties_window.h"
#include "data/blueprint.h"
#include "../parse_number.h"

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
        float k, v;
        if (!locale_safe::parse_float(str.substr(pos, colon - pos), k) ||
            !locale_safe::parse_float(str.substr(colon + 1, end - colon - 1), v)) {
            break;
        }
        keys.push_back(k);
        values.push_back(v);
        pos = end;
    }
    return !keys.empty();
}

// Serialize back to "k1:v1; k2:v2; ..." (locale-independent)
static std::string serialize_table_entries(const std::vector<float>& keys,
                                           const std::vector<float>& values) {
    std::string result;
    for (size_t i = 0; i < keys.size(); ++i) {
        if (i > 0) result += "; ";
        result += locale_safe::format_float(keys[i], "%.6g");
        result += ':';
        result += locale_safe::format_float(values[i], "%.6g");
    }
    return result;
}

void PropertiesWindow::open(Node& node, const std::string& node_id_str,
                            Blueprint& bp, UndoStack& undo_stack,
                            PropertyCallback on_apply) {
    // If already open editing a different node, revert the previous edits first
    if (open_) {
        cancelAndClose();
    }

    target_node_id_ = node_id_str;
    bp_ = &bp;
    undo_stack_ = &undo_stack;
    on_apply_ = std::move(on_apply);

    // Snapshot for cancel/revert
    snapshot_name_ = node.name;
    snapshot_params_ = node.params;

    open_ = true;
}

Node* PropertiesWindow::resolveTarget() {
    if (!bp_) return nullptr;
    return bp_->find_node(target_node_id_.c_str());
}

void PropertiesWindow::close() {
    cancelAndClose();
}

void PropertiesWindow::render() {
    if (!open_) return;

    Node* target = resolveTarget();
    if (!target) {
        // Node was deleted (e.g. by undo) while window was open — close silently
        open_ = false;
        return;
    }

#ifndef EDITOR_TESTING
    ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_FirstUseEver);
    bool window_open = true;
    if (ImGui::Begin(("Properties: " + target_node_id_).c_str(), &window_open)) {
        // Header
        ImGui::Text("%s (%s)", target_node_id_.c_str(), target->type_name.c_str());
        ImGui::Separator();

        // Name field
        char name_buf[256];
        strncpy(name_buf, target->name.c_str(), sizeof(name_buf) - 1);
        name_buf[sizeof(name_buf) - 1] = '\0';
        if (ImGui::InputText("Name", name_buf, sizeof(name_buf))) {
            target->name = name_buf;
        }

        ImGui::Separator();
        ImGui::Text("Parameters");
        ImGui::Separator();

        // Sort param keys for stable ordering
        std::vector<std::string> keys;
        keys.reserve(target->params.size());
        for (const auto& [k, _] : target->params) keys.push_back(k);
        std::sort(keys.begin(), keys.end());

        // Param fields
        for (const auto& key : keys) {
            if (key == "table") {
                renderTableParam(*target, key);
                continue;
            }
            if (key == "text") {
                // Multiline text editor for Text nodes
                char text_buf[4096];
                strncpy(text_buf, target->params[key].c_str(), sizeof(text_buf) - 1);
                text_buf[sizeof(text_buf) - 1] = '\0';
                if (ImGui::InputTextMultiline(key.c_str(), text_buf, sizeof(text_buf),
                                              ImVec2(-1, 200))) {
                    target->params[key] = text_buf;
                }
                continue;
            }
            if (key == "port_edge") {
                renderPortEdgeParam(*target, key);
                continue;
            }
            char buf[256];
            strncpy(buf, target->params[key].c_str(), sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            if (ImGui::InputText(key.c_str(), buf, sizeof(buf))) {
                target->params[key] = buf;
            }
        }

        ImGui::Separator();

        // OK / Cancel buttons
        if (ImGui::Button("OK", ImVec2(120, 0))) {
            apply();
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

void PropertiesWindow::renderPortEdgeParam(Node& node, const std::string& key) {
#ifndef EDITOR_TESTING
    const char* options[] = {"bottom", "top", "left", "right"};
    const char* labels[] = {"Bottom", "Top", "Left", "Right"};
    
    std::string& value = node.params[key];
    int current = 0;
    for (int i = 0; i < 4; ++i) {
        if (value == options[i]) {
            current = i;
            break;
        }
    }
    
    if (ImGui::BeginCombo(key.c_str(), labels[current])) {
        for (int i = 0; i < 4; ++i) {
            bool selected = (current == i);
            if (ImGui::Selectable(labels[i], selected)) {
                value = options[i];
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
#endif
}

void PropertiesWindow::renderTableParam(Node& node, const std::string& key) {
#ifndef EDITOR_TESTING
    ImGui::Text("Lookup Table");

    std::vector<float> keys, values;
    parse_table_entries(node.params[key], keys, values);

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
        node.params[key] = serialize_table_entries(keys, values);
    }
#endif
}

void PropertiesWindow::apply() {
    Node* target = resolveTarget();
    if (!target || !bp_ || !undo_stack_) {
        open_ = false;
        return;
    }

    // Collect changes: compare current params against snapshot
    auto& interner = bp_->interner();
    ui::InternedId node_iid = interner.intern(target_node_id_);

    bool has_changes = false;

    // Check for changed or added params
    for (const auto& [key, new_value] : target->params) {
        auto snap_it = snapshot_params_.find(key);
        if (snap_it == snapshot_params_.end() || snap_it->second != new_value) {
            has_changes = true;
            break;
        }
    }

    // Check for removed params (in snapshot but not in current)
    if (!has_changes) {
        for (const auto& [key, old_value] : snapshot_params_) {
            if (target->params.find(key) == target->params.end()) {
                has_changes = true;
                break;
            }
        }
    }

    // Check name change
    std::string edited_name = target->name;
    if (edited_name != snapshot_name_) {
        has_changes = true;
    }

    // If there are changes, snapshot and apply them via commands.
    // The snapshot stores the blueprint state BEFORE the apply, so undo
    // will restore the old params/name automatically.
    if (has_changes) {
        // Revert node to snapshot state, take a snapshot, then re-apply edits.
        // This ensures the undo snapshot captures the old values.
        std::string current_name = target->name;
        auto current_params = target->params;

        target->name = snapshot_name_;
        target->params = snapshot_params_;

        undo_stack_->snapshot(*bp_);

        // Apply param changes
        for (const auto& [key, new_value] : current_params) {
            auto snap_it = snapshot_params_.find(key);
            if (snap_it == snapshot_params_.end() || snap_it->second != new_value) {
                execute(*bp_, cmd_set_param(node_iid, key, new_value));
            }
        }

        // Apply removed params
        for (const auto& [key, old_value] : snapshot_params_) {
            if (current_params.find(key) == current_params.end()) {
                execute(*bp_, cmd_set_param(node_iid, key, ""));
            }
        }

        // Apply name change
        if (current_name != snapshot_name_) {
            execute(*bp_, cmd_set_name(node_iid, current_name));
        }
    }

    if (on_apply_) {
        on_apply_(target_node_id_);
    }

    open_ = false;
}

void PropertiesWindow::cancelAndClose() {
    Node* target = resolveTarget();
    if (target) {
        // Restore snapshot
        target->name = snapshot_name_;
        target->params = snapshot_params_;
    }
    open_ = false;
}
