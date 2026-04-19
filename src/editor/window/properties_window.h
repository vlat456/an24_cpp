#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "ui/core/interned_id.h"
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct ComponentRegistry;
enum class PortType;

/// Callback when properties are applied: receives the node ID
using PropertyCallback = std::function<void(const std::string& node_id)>;

/// Modal properties window for editing bp2::Blueprint::Node params via ImGui.
/// Lifecycle: open(node, callback) → render() each frame → OK or Cancel.
///
/// Shadow-editing model: the window edits local copies of the node's params,
/// name, and layout_overrides (pending_*). The live Blueprint is NEVER mutated
/// until the user clicks "OK" / apply(). On "Cancel" the pending state is
/// simply discarded.
///
/// On "OK", the window diffs pending vs snapshot, then applies all changes
/// atomically as a single undo checkpoint via push_checkpoint + replace_current.
///
/// Safety: The window stores only the node ID, never a raw Node*.
/// The node pointer is resolved fresh from the Blueprint each frame via
/// resolve_target(). This is safe across undo/redo which replaces the
/// entire Blueprint contents.
class PropertiesWindow {
public:
    void open(const bp2::Blueprint::Node& node, const std::string& node_id_str,
              bp2::EditorModel& model, ui::StringInterner& interner,
              const ComponentRegistry* type_registry,
              PropertyCallback on_apply);
    void close();
    bool is_open() const { return open_; }

    /// Apply changes and close. Diffs pending state against snapshot, emits
    /// commands to the model, then invokes the on_apply callback.
    void apply();

    /// Call every frame. Renders ImGui window when open.
    void render();

    // Test accessors
    const std::string& target_node_id_str() const { return target_node_id_; }

    /// Set a pending param value (for testing without ImGui).
    void set_pending_param(const std::string& key, float value) {
        pending_params_[key] = value;
    }

    /// Set a pending string param value (for testing without ImGui).
    void set_pending_string_param(const std::string& key, const std::string& value) {
        pending_string_params_[key] = value;
    }

    /// Set the pending name (for testing without ImGui).
    void set_pending_name(const std::string& name) {
        pending_name_ = name;
    }

    /// Set pending layout overrides (for testing without ImGui).
    void set_pending_layout_overrides(
            const std::vector<bp2::Blueprint::Node::PortLayoutOverride>& overrides) {
        pending_layout_overrides_ = overrides;
    }

    /// Set pending bridge port type for structural bridge nodes.
    void set_pending_bridge_port_type(PortType t) {
        pending_bridge_port_type_ = t;
    }

    /// Read pending bridge port type (for testing / display).
    std::optional<PortType> pending_bridge_port_type() const {
        return pending_bridge_port_type_;
    }

    /// Read pending layout overrides (for testing).
    const std::vector<bp2::Blueprint::Node::PortLayoutOverride>&
    pending_layout_overrides() const {
        return pending_layout_overrides_;
    }

    /// Read pending param value (for testing / display).
    const std::unordered_map<std::string, float>& pending_params() const {
        return pending_params_;
    }

    /// Read pending string params (for testing / display).
    const std::unordered_map<std::string, std::string>& pending_string_params() const {
        return pending_string_params_;
    }

    /// Read pending name (for testing / display).
    const std::string& pending_name() const { return pending_name_; }

private:
    bool open_ = false;
    bp2::EditorModel*    model_    = nullptr;
    ui::StringInterner*  interner_ = nullptr;
    const ComponentRegistry*  type_registry_ = nullptr;
    std::string target_node_id_;
    PropertyCallback on_apply_;

    // Shadow copies: edited by the UI, never touching the live node until apply().
    std::string pending_name_;
    std::unordered_map<std::string, float> pending_params_;
    std::unordered_map<std::string, std::string> pending_string_params_;

    // Snapshot of the node state at open() time — used for diffing at apply().
    std::string snapshot_name_;
    std::unordered_map<std::string, float> snapshot_params_;
    std::unordered_map<std::string, std::string> snapshot_string_params_;
    std::vector<bp2::Blueprint::Node::PortLayoutOverride> pending_layout_overrides_;
    std::vector<bp2::Blueprint::Node::PortLayoutOverride> snapshot_layout_overrides_;
    std::optional<PortType> pending_bridge_port_type_;
    std::optional<PortType> snapshot_bridge_port_type_;

    /// Resolve the target node from the blueprint. Returns nullptr if
    /// the node no longer exists (e.g. deleted by undo while open).
    const bp2::Blueprint::Node* resolve_target() const;

    void cancel_and_close();

    /// Render a dropdown for "port_edge" param (Bus nodes)
    void render_port_edge_param(const std::string& key);

    /// Render a generic float InputFloat for a param
    void render_generic_param(const std::string& key);

    /// Render an ImGui table editor for a LUT "table" param
    void render_table_param(const std::string& key);

    /// Render a multiline text editor for "text" param (Text nodes)
    void render_text_param(const std::string& key);

    /// Render a dropdown for "font_size" param
    void render_font_size_param(const std::string& key);

    /// Render a generic string InputText for a param
    void render_generic_string_param(const std::string& key);

    /// Render port layout override section (side/position for each port)
    void render_port_layout_section(const bp2::Blueprint::Node& node);

    /// Render a single row in the port layout table
    void render_port_layout_row(const std::string& port_name);

    /// Render PortType selector for structural bridge nodes.
    void render_bridge_port_type_section();
};
