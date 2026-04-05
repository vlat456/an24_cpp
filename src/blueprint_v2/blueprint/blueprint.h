#pragma once

#include "ui/core/interned_id.h"
#include "blueprint_v2/interface/interface.h"
#include "blueprint_v2/path/path.h"
#include "editor/data/port.h"
#include <vector>
#include <unordered_map>
#include <optional>
#include <string>
#include <memory>
#include <stdexcept>
#include <utility>

namespace bp2 {

class TypeRegistry;  // forward declaration

/// Node content type for simulation readout / interactive widgets
enum class NodeContentType {
    None,
    Gauge,
    Switch,
    VerticalToggle,
    Value,
    Text,
    Slider,
    Indicator,
    Knob
};

class Blueprint {
public:
    struct Node {
        ui::InternedId id;
        ui::InternedId type;
        Interface iface;
        std::unordered_map<ui::InternedId, float> params;
        /// String-valued parameters (e.g. font_size, text content).
        /// Kept separate from numeric params to avoid stof() failures.
        std::unordered_map<std::string, std::string> string_params;
        float x = 0.0f;
        float y = 0.0f;

        // === Editor-only fields ===
        std::string name;
        std::string render_hint;
        bool expandable = false;
        bool collapsed = true;
        std::string blueprint_path;
        std::string group_id;
        std::optional<float> width;
        std::optional<float> height;

        // Node content (simulation readout / interactive widget)
        NodeContentType content_type = NodeContentType::None;
        std::string content_label;
        float content_value = 0.0f;
        float content_min = 0.0f;
        float content_max = 1.0f;
        std::string content_unit;
        bool content_state = false;
        bool content_tripped = false;

        // Per-node custom color (has_color=false means use theme default)
        bool has_color = false;
        float color_r = 0.5f, color_g = 0.5f, color_b = 0.5f, color_a = 1.0f;

        // === Visual-layer port lists (editor only) ===
        std::vector<EditorPort> inputs;
        std::vector<EditorPort> outputs;

        // Per-port layout overrides
        struct PortLayoutOverride {
            std::string port_name;
            std::optional<std::string> side;
            std::optional<int> position;
            bool operator==(PortLayoutOverride const& o) const {
                return port_name == o.port_name && side == o.side && position == o.position;
            }
        };
        std::vector<PortLayoutOverride> layout_overrides;

        bool operator==(Node const& o) const {
            return id == o.id && type == o.type && params == o.params
                && string_params == o.string_params
                && x == o.x && y == o.y && group_id == o.group_id;
        }
    };

    struct Wire {
        ui::InternedId id;
        Path source;
        Path target;
        Domain domain = Domain::Electrical;
        std::vector<std::pair<float,float>> routing_points;

        bool operator==(Wire const& o) const {
            return id == o.id && source == o.source
                && target == o.target && domain == o.domain;
        }
    };

    struct Nested {
        ui::InternedId id;
        ui::InternedId blueprint_id;
        bool embedded = false;
        std::unique_ptr<Blueprint> inline_def;
        Interface iface;
        float x = 0.0f;
        float y = 0.0f;

        Nested() = default;
        Nested(const Nested& other);
        Nested(Nested&& other) noexcept = default;
        Nested& operator=(const Nested& other);
        Nested& operator=(Nested&& other) noexcept = default;
    };

    Blueprint() = default;
    Blueprint(Blueprint const& other);
    Blueprint(Blueprint&& other) noexcept;
    Blueprint& operator=(Blueprint const& other);
    Blueprint& operator=(Blueprint&& other) noexcept;

    ui::InternedId id() const { return id_; }
    std::string const& display_name() const { return display_name_; }
    Interface const& iface() const { return iface_; }

    std::vector<Node> const& nodes() const { return nodes_; }
    std::vector<Wire> const& wires() const { return wires_; }
    std::vector<Nested> const& nested() const { return nested_; }

    Node const* find_node(ui::InternedId id) const;
    Wire const* find_wire(ui::InternedId id) const;
    Nested const* find_nested(ui::InternedId id) const;

    Blueprint with_node(Node n) const;
    Blueprint without_node(ui::InternedId id) const;
    Blueprint with_wire(Wire w) const;
    Blueprint without_wire(ui::InternedId id) const;
    Blueprint with_nested(Nested n) const;
    Blueprint without_nested(ui::InternedId id) const;
    Blueprint with_id(ui::InternedId id) const;
    Blueprint with_display_name(std::string name) const;
    Blueprint with_interface(Interface iface) const;
    Blueprint clone(ui::InternedId new_id) const;

    // === Viewport state accessors ===
    float pan_x() const { return pan_x_; }
    float pan_y() const { return pan_y_; }
    float zoom() const { return zoom_; }
    float grid_step() const { return grid_step_; }
    std::string const& name() const { return name_; }

    Blueprint with_viewport(float pan_x, float pan_y, float zoom, float grid_step) const;
    Blueprint with_name(std::string n) const;

    /// Returns all (path, port) pairs reachable from this blueprint.
    std::vector<std::pair<Path, PortDescriptor>> all_ports(PathArena& arena) const;

    /// Validates all invariants. Throws std::runtime_error on failure.
    void validate(TypeRegistry const& registry) const;
    void validate(TypeRegistry const& registry, PathArena const& arena) const;

    bool operator==(Blueprint const& other) const;
    bool operator!=(Blueprint const& other) const { return !(*this == other); }

private:
    ui::InternedId id_;
    std::string display_name_;
    Interface iface_;
    std::vector<Node> nodes_;
    std::vector<Wire> wires_;
    std::vector<Nested> nested_;

    // Editor viewport state (saved with document)
    float pan_x_ = 0.0f;
    float pan_y_ = 0.0f;
    float zoom_ = 1.0f;
    float grid_step_ = 16.0f;
    std::string name_;

    mutable std::unordered_map<ui::InternedId, size_t> node_idx_;
    mutable bool node_idx_valid_ = false;
    mutable std::unordered_map<ui::InternedId, size_t> wire_idx_;
    mutable bool wire_idx_valid_ = false;
    mutable std::unordered_map<ui::InternedId, size_t> nested_idx_;
    mutable bool nested_idx_valid_ = false;

    void ensure_node_index() const;
    void ensure_wire_index() const;
    void ensure_nested_index() const;

    static bool nested_equals(Nested const& a, Nested const& b);

    void collect_ports_recursive(
        std::vector<std::pair<Path, PortDescriptor>>& result,
        PathArena& arena,
        Path prefix) const;
};

} // namespace bp2
