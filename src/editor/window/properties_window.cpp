#include "window/properties_window.h"
#include "data/blueprint.h"
#include "../parse_number.h"

#ifndef EDITOR_TESTING
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
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
    // If already open editing a different node, just close (shadow editing
    // means no live mutations to revert).
    if (open_) {
        open_ = false;
    }

    target_node_id_ = node_id_str;
    bp_ = &bp;
    undo_stack_ = &undo_stack;
    on_apply_ = std::move(on_apply);

    // Snapshot for diffing at apply() time
    snapshot_name_ = node.name;
    snapshot_params_ = node.params;
    snapshot_layout_overrides_ = node.layout_overrides;

    // Initialize pending copies from the live node
    pending_name_ = node.name;
    pending_params_ = node.params;
    pending_layout_overrides_ = node.layout_overrides;

    open_ = true;
}

Node* PropertiesWindow::resolve_target() {
    if (!bp_) return nullptr;
    return bp_->find_node(target_node_id_);
}

void PropertiesWindow::close() {
    cancel_and_close();
}

void PropertiesWindow::render() {
    if (!open_) return;

    Node* target = resolve_target();
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

        // Name field — edits pending_name_, not the live node
        ImGui::InputText("Name", &pending_name_);

        ImGui::Separator();
        ImGui::Text("Parameters");
        ImGui::Separator();

        // Sort param keys for stable ordering
        std::vector<std::string> keys;
        keys.reserve(pending_params_.size());
        for (const auto& [k, _] : pending_params_) keys.push_back(k);
        std::sort(keys.begin(), keys.end());

        // Param fields — all edit pending_params_, not the live node
        for (const auto& key : keys) {
            if (key == "table") {
                render_table_param(key);
            } else if (key == "text") {
                render_text_param(key);
            } else if (key == "font_size") {
                render_font_size_param(key);
            } else if (key == "port_edge") {
                render_port_edge_param(key);
            } else {
                render_generic_param(key);
            }
        }

        // Port layout section (not for Bus nodes)
        render_port_layout_section(*target);

        ImGui::Separator();

        // OK / Cancel buttons
        if (ImGui::Button("OK", ImVec2(120, 0))) {
            apply();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            cancel_and_close();
        }
    }
    ImGui::End();

    // Window closed via X button
    if (!window_open) {
        cancel_and_close();
    }
#endif
}

void PropertiesWindow::render_port_edge_param(const std::string& key) {
#ifndef EDITOR_TESTING
    const char* options[] = {"bottom", "top", "left", "right"};
    const char* labels[] = {"Bottom", "Top", "Left", "Right"};
    
    std::string& value = pending_params_[key];
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

void PropertiesWindow::render_table_param(const std::string& key) {
#ifndef EDITOR_TESTING
    ImGui::Text("Lookup Table");

    std::vector<float> keys, values;
    parse_table_entries(pending_params_[key], keys, values);

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
        pending_params_[key] = serialize_table_entries(keys, values);
    }
#endif
}

void PropertiesWindow::render_text_param(const std::string& key) {
#ifndef EDITOR_TESTING
    ImGui::InputTextMultiline(key.c_str(), &pending_params_[key],
                              ImVec2(-1, 200));
#endif
}

void PropertiesWindow::render_font_size_param(const std::string& key) {
#ifndef EDITOR_TESTING
    const char* options[] = {"small", "medium", "large"};
    const char* labels[]  = {"Small", "Medium", "Large"};

    std::string& value = pending_params_[key];
    int current = 2;  // default: large
    for (int i = 0; i < 3; ++i) {
        if (value == options[i]) {
            current = i;
            break;
        }
    }

    if (ImGui::BeginCombo(key.c_str(), labels[current])) {
        for (int i = 0; i < 3; ++i) {
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

void PropertiesWindow::render_generic_param(const std::string& key) {
#ifndef EDITOR_TESTING
    ImGui::InputText(key.c_str(), &pending_params_[key]);
#endif
}

void PropertiesWindow::render_port_layout_row(const std::string& port_name) {
#ifndef EDITOR_TESTING
    ImGui::TableNextRow();
    ImGui::PushID(port_name.c_str());

    // Find existing override for this port
    auto it = std::find_if(pending_layout_overrides_.begin(),
                           pending_layout_overrides_.end(),
                           [&](const PortLayoutOverride& o) { return o.port_name == port_name; });

    // Port name column
    ImGui::TableNextColumn();
    ImGui::Text("%s", port_name.c_str());

    // Side dropdown column
    ImGui::TableNextColumn();
    {
        const char* options[] = {"Auto", "Left", "Right", "Top", "Bottom"};
        int current = 0;  // Auto
        if (it != pending_layout_overrides_.end() && it->side.has_value()) {
            current = static_cast<int>(*it->side) + 1;
        }

        if (ImGui::Combo("##side", &current, options, IM_ARRAYSIZE(options))) {
            if (it == pending_layout_overrides_.end()) {
                pending_layout_overrides_.push_back({port_name, std::nullopt, std::nullopt});
                it = pending_layout_overrides_.end() - 1;
            }
            if (current == 0) {
                it->side = std::nullopt;
            } else {
                it->side = static_cast<PortLayoutSide>(current - 1);
            }
        }
    }

    // Position input column
    ImGui::TableNextColumn();
    {
        int pos = -1;  // -1 means auto
        if (it != pending_layout_overrides_.end() && it->position.has_value()) {
            pos = static_cast<int>(*it->position);
        }

        if (ImGui::InputInt("##pos", &pos, 1, 1)) {
            if (it == pending_layout_overrides_.end()) {
                pending_layout_overrides_.push_back({port_name, std::nullopt, std::nullopt});
                it = pending_layout_overrides_.end() - 1;
            }
            if (pos < 0) {
                it->position = std::nullopt;
            } else {
                it->position = static_cast<uint8_t>(std::min(pos, 255));
            }
        }
    }

    // Reset button column
    ImGui::TableNextColumn();
    if (ImGui::SmallButton("Reset")) {
        if (it != pending_layout_overrides_.end()) {
            pending_layout_overrides_.erase(it);
        }
    }

    ImGui::PopID();
#endif
}

void PropertiesWindow::render_port_layout_section(const Node& node) {
#ifndef EDITOR_TESTING
    // Skip for Bus nodes - they have their own port_edge mechanism
    if (node.render_hint == "bus") return;

    // Skip if node has no ports
    if (node.inputs.empty() && node.outputs.empty()) return;

    ImGui::Separator();
    ImGui::Text("Port Layout");
    ImGui::Separator();

    // Collect all port names
    std::vector<std::string> all_ports;
    for (const auto& p : node.inputs) {
        all_ports.push_back(std::string(bp_->interner().resolve(p.name)));
    }
    for (const auto& p : node.outputs) {
        all_ports.push_back(std::string(bp_->interner().resolve(p.name)));
    }

    // Table: Port | Side | Position | Reset
    if (ImGui::BeginTable("port_layout", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Port");
        ImGui::TableSetupColumn("Side");
        ImGui::TableSetupColumn("Pos");
        ImGui::TableSetupColumn("##reset", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableHeadersRow();

        for (const auto& port_name : all_ports) {
            render_port_layout_row(port_name);
        }

        ImGui::EndTable();
    }

    // Clean up orphaned overrides (ports that no longer exist)
    auto orphan_it = std::remove_if(pending_layout_overrides_.begin(), pending_layout_overrides_.end(),
        [&](const PortLayoutOverride& o) {
            return std::find(all_ports.begin(), all_ports.end(), o.port_name) == all_ports.end();
        });
    pending_layout_overrides_.erase(orphan_it, pending_layout_overrides_.end());
#endif
}

void PropertiesWindow::apply() {
    Node* target = resolve_target();
    if (!target || !bp_ || !undo_stack_) {
        open_ = false;
        return;
    }

    // Diff pending state against snapshot to detect changes
    auto& interner = bp_->interner();
    ui::InternedId node_iid = interner.intern(target_node_id_);

    bool has_changes = false;

    // Check for changed or added params
    for (const auto& [key, new_value] : pending_params_) {
        auto snap_it = snapshot_params_.find(key);
        if (snap_it == snapshot_params_.end() || snap_it->second != new_value) {
            has_changes = true;
            break;
        }
    }

    // Check for removed params (in snapshot but not in pending)
    if (!has_changes) {
        for (const auto& [key, old_value] : snapshot_params_) {
            if (pending_params_.find(key) == pending_params_.end()) {
                has_changes = true;
                break;
            }
        }
    }

    // Check name change
    if (pending_name_ != snapshot_name_) {
        has_changes = true;
    }
    
    // Check layout_overrides change
    if (pending_layout_overrides_ != snapshot_layout_overrides_) {
        has_changes = true;
    }

    // If there are changes, snapshot and apply them via commands.
    if (has_changes) {
        // Take a snapshot of the current (unmodified) blueprint state.
        // The live node still has the original values since we use shadow editing.
        undo_stack_->snapshot(*bp_);

        // Apply param changes via commands
        for (const auto& [key, new_value] : pending_params_) {
            auto snap_it = snapshot_params_.find(key);
            if (snap_it == snapshot_params_.end() || snap_it->second != new_value) {
                execute(*bp_, cmd_set_param(node_iid, key, new_value));
            }
        }

        // Apply removed params
        for (const auto& [key, old_value] : snapshot_params_) {
            if (pending_params_.find(key) == pending_params_.end()) {
                execute(*bp_, cmd_set_param(node_iid, key, ""));
            }
        }

        // Apply name change
        if (pending_name_ != snapshot_name_) {
            execute(*bp_, cmd_set_name(node_iid, pending_name_));
        }
        
        // Apply layout overrides change
        if (pending_layout_overrides_ != snapshot_layout_overrides_) {
            execute(*bp_, cmd_set_port_layout(node_iid, pending_layout_overrides_));
        }
    }

    if (on_apply_) {
        on_apply_(target_node_id_);
    }

    open_ = false;
}

void PropertiesWindow::cancel_and_close() {
    // Shadow editing: the live node was never touched, so no revert needed.
    open_ = false;
}
