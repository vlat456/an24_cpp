#include "properties_window.h"
#include "editor/common/port_type_utils.h"
#include "blueprint_v2/interface/node_port_projection.h"
#include "parse_number.h"

#ifndef EDITOR_TESTING
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#endif

#include <algorithm>
#include <sstream>
#include <vector>

static bool is_bridge_node_type(ui::StringInterner& interner, ui::InternedId type) {
    return type == interner.intern("BlueprintInput")
        || type == interner.intern("BlueprintOutput");
}

static const char* port_type_label(PortType t) {
    switch (t) {
        case PortType::V: return "Voltage (V)";
        case PortType::I: return "Current (I)";
        case PortType::Bool: return "Bool";
        case PortType::RPM: return "RPM";
        case PortType::Temperature: return "Temperature";
        case PortType::Pressure: return "Pressure";
        case PortType::Position: return "Position";
        case PortType::Any: return "Any";
    }
    return "Any";
}

static const std::vector<PortType>& all_port_types() {
    static const std::vector<PortType> kTypes = {
        PortType::V,
        PortType::I,
        PortType::Bool,
        PortType::RPM,
        PortType::Temperature,
        PortType::Pressure,
        PortType::Position,
        PortType::Any,
    };
    return kTypes;
}

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
    snapshot_string_params_.clear();
    pending_string_params_.clear();
    for (const auto& [key_iid, val] : node.semantic.params) {
        std::string key_str = std::string(interner_->resolve(key_iid));
        snapshot_params_[key_str] = val;
        pending_params_[key_str]  = val;
    }
    for (const auto& [key, val] : node.semantic.string_params) {
        snapshot_string_params_[key] = val;
        pending_string_params_[key] = val;
    }

    // Snapshot for diffing at apply() time
    snapshot_name_ = node.view.name;
    snapshot_layout_overrides_ = node.layout.layout_overrides;

    // Initialize pending copies from the live node
    pending_name_ = node.view.name;
    pending_layout_overrides_ = node.layout.layout_overrides;
    snapshot_bridge_port_type_.reset();
    pending_bridge_port_type_.reset();
    if (is_bridge_node_type(*interner_, node.semantic.type)) {
        const auto in_ports = bp2::derive_input_ports(node.semantic.iface);
        const auto out_ports = bp2::derive_output_ports(node.semantic.iface);
        if (!in_ports.empty()) {
            snapshot_bridge_port_type_ = in_ports.front().type;
            pending_bridge_port_type_ = in_ports.front().type;
        } else if (!out_ports.empty()) {
            snapshot_bridge_port_type_ = out_ports.front().type;
            pending_bridge_port_type_ = out_ports.front().type;
        }
    }

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
        std::string type_str = std::string(interner_->resolve(target->semantic.type));
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

        std::vector<std::string> string_keys;
        string_keys.reserve(pending_string_params_.size());
        for (const auto& [k, _] : pending_string_params_) string_keys.push_back(k);
        std::sort(string_keys.begin(), string_keys.end());

        // Determine if this is a bridge node so we can skip exposed_type/
        // exposed_direction — those are edited via the dedicated PortType
        // dropdown rendered by render_bridge_port_type_section() below.
        const bool is_bridge = target && is_bridge_node_type(*interner_, target->semantic.type);

        for (const auto& key : string_keys) {
            if (key == "table") {
                render_table_param(key);
            } else if (key == "text") {
                render_text_param(key);
            } else if (key == "font_size") {
                render_font_size_param(key);
            } else if (key == "port_edge") {
                render_port_edge_param(key);
            } else if (is_bridge && (key == "exposed_type" || key == "exposed_direction")) {
                // Skip — handled by render_bridge_port_type_section()
            } else {
                render_generic_string_param(key);
            }
        }

        // Port layout section
        render_port_layout_section(*target);

        // Bridge PortType section (BlueprintInput/BlueprintOutput)
        render_bridge_port_type_section();

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

    int current = 0;
    auto fit = pending_params_.find(key);
    if (fit != pending_params_.end()) {
        current = static_cast<int>(fit->second);
        if (current < 0 || current > 3) current = 0;
    } else {
        auto sit = pending_string_params_.find(key);
        if (sit != pending_string_params_.end()) {
            for (int i = 0; i < 4; ++i) {
                if (sit->second == options[i]) {
                    current = i;
                    break;
                }
            }
        }
    }

    if (ImGui::BeginCombo(key.c_str(), labels[current])) {
        for (int i = 0; i < 4; ++i) {
            bool selected = (current == i);
            if (ImGui::Selectable(labels[i], selected)) {
                if (fit != pending_params_.end()) {
                    fit->second = static_cast<float>(i);
                } else {
                    pending_string_params_[key] = options[i];
                }
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

void PropertiesWindow::render_table_param(const std::string& key) {
#ifndef EDITOR_TESTING
    ImGui::Text("Lookup Table");

    std::vector<float> keys, values;
    parse_table_entries(pending_string_params_[key], keys, values);

    if (keys.empty()) {
        keys.push_back(0.0f);
        values.push_back(0.0f);
    }

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
        pending_string_params_[key] = serialize_table_entries(keys, values);
    }
#endif
}

void PropertiesWindow::render_text_param(const std::string& key) {
#ifndef EDITOR_TESTING
    ImGui::InputTextMultiline(key.c_str(), &pending_string_params_[key], ImVec2(-1, 200));
#endif
}

void PropertiesWindow::render_font_size_param(const std::string& key) {
#ifndef EDITOR_TESTING
    const char* options[] = {"small", "medium", "large"};
    const char* labels[]  = {"Small", "Medium", "Large"};

    std::string& value = pending_string_params_[key];
    int current = 2;
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

void PropertiesWindow::render_generic_string_param(const std::string& key) {
#ifndef EDITOR_TESTING
    ImGui::InputText(key.c_str(), &pending_string_params_[key]);
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
    if (node.view.render_hint == "bus") return;

    // Skip if node has no ports
    const auto in_ports = bp2::derive_input_ports(node.semantic.iface);
    const auto out_ports = bp2::derive_output_ports(node.semantic.iface);
    if (in_ports.empty() && out_ports.empty()) return;

    ImGui::Separator();
    ImGui::Text("Port Layout");
    ImGui::Separator();

    // Collect all port names
    std::vector<std::string> all_ports;
    for (const auto& p : in_ports) {
        all_ports.push_back(std::string(interner_->resolve(p.name)));
    }
    for (const auto& p : out_ports) {
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

    if (!has_changes) {
        for (const auto& [key, new_value] : pending_string_params_) {
            auto snap_it = snapshot_string_params_.find(key);
            if (snap_it == snapshot_string_params_.end() || snap_it->second != new_value) {
                has_changes = true;
                break;
            }
        }
    }

    if (!has_changes) {
        for (const auto& [key, old_value] : snapshot_string_params_) {
            if (pending_string_params_.find(key) == pending_string_params_.end()) {
                has_changes = true;
                break;
            }
        }
    }

    // Check layout_overrides change
    if (pending_layout_overrides_ != snapshot_layout_overrides_) {
        has_changes = true;
    }

    if (pending_bridge_port_type_ != snapshot_bridge_port_type_) {
        has_changes = true;
    }

    if (has_changes) {
        // Build a single modified node, then commit all changes in one checkpoint.
        // This guarantees a single undo step regardless of how many fields changed.
        bp2::Blueprint::Node updated = *target;

        // Apply all pending params
        updated.semantic.params.clear();
        for (const auto& [key, new_value] : pending_params_) {
            ui::InternedId key_iid = interner_->intern(key);
            updated.semantic.params[key_iid] = new_value;
        }

        updated.semantic.string_params = pending_string_params_;

        // Sync content_max / content_min from params for interactive widget types.
        // When the user changes e.g. "positions" in the inspector, the bp2 node's
        // content_max must be updated so visual widgets (KnobWidget tick marks,
        // SliderWidget range, VoltmeterWidget range) reflect the new value.
        // [BUG-1] Without this, changing positions 2→5 left ticks unchanged.
        if (updated.view.content_type == bp2::NodeContentType::Knob) {
            auto pos_key = interner_->intern("positions");
            auto it = updated.semantic.params.find(pos_key);
            if (it != updated.semantic.params.end()) {
                updated.view.content_max = it->second;
            }
        } else if (updated.view.content_type == bp2::NodeContentType::Slider
                || updated.view.content_type == bp2::NodeContentType::Gauge) {
            auto min_key = interner_->intern("min");
            auto max_key = interner_->intern("max");
            auto min_it = updated.semantic.params.find(min_key);
            auto max_it = updated.semantic.params.find(max_key);
            if (min_it != updated.semantic.params.end()) updated.view.content_min = min_it->second;
            if (max_it != updated.semantic.params.end()) updated.view.content_max = max_it->second;
        }

        // Apply name change
        updated.view.name = pending_name_;

        // Apply layout overrides change
        updated.layout.layout_overrides = pending_layout_overrides_;

        // Apply bridge PortType change to all ports (ext/port share semantics).
        if (pending_bridge_port_type_.has_value() && is_bridge_node_type(*interner_, updated.semantic.type)) {
            std::vector<bp2::PortDescriptor> ports = updated.semantic.iface.ports();
            for (auto& pd : ports) {
                pd.port_type = *pending_bridge_port_type_;
                pd.domain = editor::common::domain_for_port_type(*pending_bridge_port_type_);
            }
            updated.semantic.iface = bp2::Interface(std::move(ports));
        }

        // Single atomic checkpoint + replace
        bp2::Blueprint next_bp = bp2::replace_node_preserve_order(model_->current(), std::move(updated));

        // If editing an extracted bridge node (<nested_id>:<iface_name>), propagate
        // type/domain to the parent collapsed node port and nested iface descriptor
        // so top-level port colors/types stay consistent.
        if (pending_bridge_port_type_.has_value()) {
            const std::string& full_id = target_node_id_;
            const size_t sep = full_id.find(':');
            if (sep != std::string::npos && interner_) {
                const std::string nested_id_str = full_id.substr(0, sep);
                const std::string iface_name = full_id.substr(sep + 1);
                const ui::InternedId nested_iid = interner_->lookup(nested_id_str);
                const ui::InternedId iface_iid = interner_->lookup(iface_name);

                if (!nested_iid.empty() && !iface_iid.empty()) {
                    // Update parent collapsed node port types
                    if (const auto* collapsed = next_bp.find_node(nested_iid)) {
                        bp2::Blueprint::Node n = *collapsed;
                        std::vector<bp2::PortDescriptor> ports = n.semantic.iface.ports();
                        const Domain d = editor::common::domain_for_port_type(*pending_bridge_port_type_);
                        for (auto& pd : ports) {
                            if (pd.name == iface_iid) {
                                pd.port_type = *pending_bridge_port_type_;
                                pd.domain = d;
                            }
                        }
                        n.semantic.iface = bp2::Interface(std::move(ports));
                        next_bp = bp2::replace_node_preserve_order(next_bp, std::move(n));
                    }

                    // Update nested iface domain for the corresponding boundary port
                    if (const auto* nested = next_bp.find_nested(nested_iid)) {
                        bp2::Blueprint::Nested n = *nested;
                        if (n.is_embedded() && n.inline_def()) {
                            std::vector<bp2::PortDescriptor> ports = n.inline_def()->iface().ports();
                            const Domain d = editor::common::domain_for_port_type(*pending_bridge_port_type_);
                            for (auto& pd : ports) {
                                if (pd.name == iface_iid) {
                                    pd.domain = d;
                                    pd.port_type = *pending_bridge_port_type_;
                                }
                            }
                            bp2::Blueprint updated_inline = n.inline_def()->with_interface(bp2::Interface(std::move(ports)));
                            n.set_inline_def(std::make_unique<bp2::Blueprint>(std::move(updated_inline)));
                            next_bp = bp2::replace_nested_preserve_order(next_bp, std::move(n));
                        }
                    }
                }
            }
        }

        model_->push_checkpoint();
        model_->replace_current(std::move(next_bp));
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

void PropertiesWindow::render_bridge_port_type_section() {
#ifndef EDITOR_TESTING
    const bp2::Blueprint::Node* target = resolve_target();
    if (!target || !interner_) return;
    if (!is_bridge_node_type(*interner_, target->semantic.type)) return;
    if (!pending_bridge_port_type_.has_value()) return;

    ImGui::Separator();
    ImGui::Text("Port Type");
    ImGui::Separator();

    const auto& types = all_port_types();
    int current = 0;
    for (size_t i = 0; i < types.size(); ++i) {
        if (types[i] == *pending_bridge_port_type_) {
            current = static_cast<int>(i);
            break;
        }
    }

    if (ImGui::BeginCombo("Port Type", port_type_label(types[static_cast<size_t>(current)]))) {
        for (size_t i = 0; i < types.size(); ++i) {
            bool selected = (static_cast<int>(i) == current);
            if (ImGui::Selectable(port_type_label(types[i]), selected)) {
                pending_bridge_port_type_ = types[i];
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
#endif
}
