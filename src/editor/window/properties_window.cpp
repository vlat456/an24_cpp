#include "properties_window.h"

#ifndef EDITOR_TESTING
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#endif

#include <algorithm>
#include <vector>

void PropertiesWindow::open(const bp2::Blueprint::Node& node,
                             const std::string& node_id_str,
                             bp2::EditorModel& model,
                             ui::StringInterner& interner,
                             PropertyCallback on_apply) {
    // If already open editing a different node, just close (shadow editing
    // means no live mutations to revert).
    if (open_) {
        open_ = false;
    }

    target_node_id_ = node_id_str;
    model_    = &model;
    interner_ = &interner;
    on_apply_ = std::move(on_apply);

    // Build string→float maps from InternedId→float params
    snapshot_params_.clear();
    pending_params_.clear();
    for (const auto& [key_iid, val] : node.params) {
        std::string key_str = std::string(interner_->resolve(key_iid));
        snapshot_params_[key_str] = val;
        pending_params_[key_str]  = val;
    }

    // Snapshot for diffing at apply() time
    snapshot_name_ = node.name;
    snapshot_layout_overrides_ = node.layout_overrides;

    // Initialize pending copies from the live node
    pending_name_ = node.name;
    pending_layout_overrides_ = node.layout_overrides;

    open_ = true;
}

const bp2::Blueprint::Node* PropertiesWindow::resolve_target() const {
    if (!model_ || !interner_) return nullptr;
    ui::InternedId iid = interner_->lookup(target_node_id_);
    if (iid.empty()) return nullptr;
    return model_->current().find_node(iid);
}

void PropertiesWindow::close() {
    cancel_and_close();
}

void PropertiesWindow::render() {
    if (!open_) return;

    const bp2::Blueprint::Node* target = resolve_target();
    if (!target) {
        // Node was deleted (e.g. by undo) while window was open — close silently
        open_ = false;
        return;
    }

#ifndef EDITOR_TESTING
    ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_FirstUseEver);
    bool window_open = true;
    if (ImGui::Begin(("Properties: " + target_node_id_).c_str(), &window_open)) {
        // Header: resolve type name from InternedId
        std::string type_str = std::string(interner_->resolve(target->type));
        ImGui::Text("%s (%s)", target_node_id_.c_str(), type_str.c_str());
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
            if (key == "port_edge") {
                render_port_edge_param(key);
            } else {
                render_generic_param(key);
            }
        }

        // Port layout section
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
    const char* labels[]  = {"Bottom", "Top", "Left", "Right"};

    // port_edge is stored as a float index (0–3) into the options array
    float& fval = pending_params_[key];
    int current = static_cast<int>(fval);
    if (current < 0 || current > 3) current = 0;

    if (ImGui::BeginCombo(key.c_str(), labels[current])) {
        for (int i = 0; i < 4; ++i) {
            bool selected = (current == i);
            if (ImGui::Selectable(labels[i], selected)) {
                fval = static_cast<float>(i);
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
    ImGui::InputFloat(key.c_str(), &pending_params_[key]);
#endif
}

void PropertiesWindow::render_port_layout_row(const std::string& port_name) {
#ifndef EDITOR_TESTING
    ImGui::TableNextRow();
    ImGui::PushID(port_name.c_str());

    // Find existing override for this port
    auto it = std::find_if(pending_layout_overrides_.begin(),
                           pending_layout_overrides_.end(),
                           [&](const bp2::Blueprint::Node::PortLayoutOverride& o) {
                               return o.port_name == port_name;
                           });

    // Port name column
    ImGui::TableNextColumn();
    ImGui::Text("%s", port_name.c_str());

    // Side dropdown column
    ImGui::TableNextColumn();
    {
        const char* options[] = {"Auto", "Left", "Right", "Top", "Bottom"};
        int current = 0;  // Auto
        if (it != pending_layout_overrides_.end() && it->side.has_value()) {
            const std::string& s = *it->side;
            if (s == "left")   current = 1;
            else if (s == "right")  current = 2;
            else if (s == "top")    current = 3;
            else if (s == "bottom") current = 4;
        }

        if (ImGui::Combo("##side", &current, options, IM_ARRAYSIZE(options))) {
            if (it == pending_layout_overrides_.end()) {
                pending_layout_overrides_.push_back({port_name, std::nullopt, std::nullopt});
                it = pending_layout_overrides_.end() - 1;
            }
            if (current == 0) {
                it->side = std::nullopt;
            } else {
                static const char* side_names[] = {"left", "right", "top", "bottom"};
                it->side = side_names[current - 1];
            }
        }
    }

    // Position input column
    ImGui::TableNextColumn();
    {
        int pos = -1;  // -1 means auto
        if (it != pending_layout_overrides_.end() && it->position.has_value()) {
            pos = *it->position;
        }

        if (ImGui::InputInt("##pos", &pos, 1, 1)) {
            if (it == pending_layout_overrides_.end()) {
                pending_layout_overrides_.push_back({port_name, std::nullopt, std::nullopt});
                it = pending_layout_overrides_.end() - 1;
            }
            if (pos < 0) {
                it->position = std::nullopt;
            } else {
                it->position = pos;
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

void PropertiesWindow::render_port_layout_section(const bp2::Blueprint::Node& node) {
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
        all_ports.push_back(std::string(interner_->resolve(p.name)));
    }
    for (const auto& p : node.outputs) {
        all_ports.push_back(std::string(interner_->resolve(p.name)));
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
    auto orphan_it = std::remove_if(pending_layout_overrides_.begin(),
                                    pending_layout_overrides_.end(),
        [&](const bp2::Blueprint::Node::PortLayoutOverride& o) {
            return std::find(all_ports.begin(), all_ports.end(), o.port_name) == all_ports.end();
        });
    pending_layout_overrides_.erase(orphan_it, pending_layout_overrides_.end());
#endif
}

void PropertiesWindow::apply() {
    const bp2::Blueprint::Node* target = resolve_target();
    if (!target || !model_ || !interner_) {
        open_ = false;
        return;
    }

    ui::InternedId node_iid = interner_->intern(target_node_id_);

    bool has_changes = false;

    // Check for changed or added params
    for (const auto& [key, new_value] : pending_params_) {
        auto snap_it = snapshot_params_.find(key);
        if (snap_it == snapshot_params_.end() || snap_it->second != new_value) {
            has_changes = true;
            break;
        }
    }

    // Check for removed params
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

    if (has_changes) {
        model_->push_checkpoint();

        // Apply param changes
        for (const auto& [key, new_value] : pending_params_) {
            auto snap_it = snapshot_params_.find(key);
            if (snap_it == snapshot_params_.end() || snap_it->second != new_value) {
                ui::InternedId key_iid = interner_->intern(key);
                execute(*model_, *interner_, cmd_set_param(node_iid, key_iid, new_value));
            }
        }

        // Apply name change
        if (pending_name_ != snapshot_name_) {
            execute(*model_, *interner_, cmd_set_name(node_iid, pending_name_));
        }

        // Apply layout overrides change
        if (pending_layout_overrides_ != snapshot_layout_overrides_) {
            execute(*model_, *interner_,
                    cmd_set_port_layout(node_iid, pending_layout_overrides_));
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
