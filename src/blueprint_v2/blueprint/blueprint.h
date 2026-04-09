#pragma once

#include "ui/core/interned_id.h"
#include "blueprint_v2/interface/interface.h"
#include "blueprint_v2/path/path.h"
#include "blueprint_v2/blueprint/node_content_type.h"
#include <variant>
#include <vector>
#include <unordered_map>
#include <optional>
#include <string>
#include <memory>
#include <stdexcept>
#include <utility>

struct TypeRegistry;

namespace bp2 {

class Blueprint {
public:
    struct Node {
        // Per-port layout overrides (used by LayoutData)
        struct PortLayoutOverride {
            std::string port_name;
            std::optional<std::string> side;
            std::optional<int> position;
            bool operator==(PortLayoutOverride const& o) const {
                return port_name == o.port_name && side == o.side && position == o.position;
            }
        };

        // === Semantic/behavioral data ===
        struct SemanticData {
            ui::InternedId id;
            ui::InternedId type;
            /// For composite host nodes this is a derived cache that must mirror
            /// the hosted nested resolved interface. Use effective_node_iface()
            /// for authoritative reads.
            Interface iface;
            std::unordered_map<ui::InternedId, float> params;
            /// String-valued parameters (e.g. font_size, text content).
            /// Kept separate from numeric params to avoid stof() failures.
            std::unordered_map<std::string, std::string> string_params;

            bool operator==(SemanticData const& o) const {
                return id == o.id && type == o.type && iface == o.iface
                    && params == o.params && string_params == o.string_params;
            }
        };

        // === Layout/positioning data ===
        struct LayoutData {
            float x = 0.0f;
            float y = 0.0f;
            bool collapsed = true;
            std::string layout_group;
            std::optional<float> width;
            std::optional<float> height;
            std::vector<PortLayoutOverride> layout_overrides;

            bool operator==(LayoutData const& o) const {
                return x == o.x && y == o.y && collapsed == o.collapsed
                    && layout_group == o.layout_group && width == o.width && height == o.height
                    && layout_overrides == o.layout_overrides;
            }
        };

        // === View/presentation data ===
        struct ViewData {
            std::string name;
            std::string render_hint;
            bool expandable = false;
            std::string blueprint_path;

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

            bool operator==(ViewData const& o) const {
                return name == o.name && render_hint == o.render_hint
                    && expandable == o.expandable && blueprint_path == o.blueprint_path
                    && content_type == o.content_type && content_label == o.content_label
                    && content_value == o.content_value && content_min == o.content_min
                    && content_max == o.content_max && content_unit == o.content_unit
                    && content_state == o.content_state && content_tripped == o.content_tripped
                    && has_color == o.has_color && color_r == o.color_r && color_g == o.color_g
                    && color_b == o.color_b && color_a == o.color_a;
            }
        };

        SemanticData semantic;
        LayoutData layout;
        ViewData view;

        bool operator==(Node const& o) const {
            return semantic == o.semantic && layout == o.layout && view == o.view;
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
        float x = 0.0f;
        float y = 0.0f;

        /// Embedded mode: owned inline blueprint definition.
        struct Embedded {
            ui::InternedId blueprint_id;
            std::unique_ptr<Blueprint> inline_def;  // always non-null

            Embedded() = delete;
            Embedded(ui::InternedId bp_id, std::unique_ptr<Blueprint> def)
                : blueprint_id(bp_id), inline_def(std::move(def)) {}
            Embedded(const Embedded& other);
            Embedded(Embedded&&) noexcept = default;
            Embedded& operator=(const Embedded& other);
            Embedded& operator=(Embedded&&) noexcept = default;
        };

        /// Reference mode: external blueprint by ID with resolved interface cache.
        struct Reference {
            ui::InternedId blueprint_id;  // always non-empty
            Interface resolved_iface;     // always populated
        };

    private:
        std::variant<Embedded, Reference> content_;

        Nested(ui::InternedId nid, float px, float py, std::variant<Embedded, Reference> c)
            : id(nid), x(px), y(py), content_(std::move(c)) {}

    public:

        // ── Factory methods ──

        static Nested make_embedded(ui::InternedId id,
                                    ui::InternedId blueprint_id,
                                    std::unique_ptr<Blueprint> inline_def,
                                    float x = 0.0f, float y = 0.0f) {
            if (!inline_def) {
                throw std::logic_error("Nested::make_embedded requires non-null inline_def");
            }
            return Nested{id, x, y, Embedded{blueprint_id, std::move(inline_def)}};
        }

        static Nested make_reference(ui::InternedId id,
                                     ui::InternedId blueprint_id,
                                     Interface resolved_iface,
                                     float x = 0.0f, float y = 0.0f) {
            if (blueprint_id.empty()) {
                throw std::logic_error("Nested::make_reference requires non-empty blueprint_id");
            }
            return Nested{id, x, y, Reference{blueprint_id, std::move(resolved_iface)}};
        }

        // ── Accessors ──

        bool is_embedded() const { return std::holds_alternative<Embedded>(content_); }
        bool is_reference() const { return std::holds_alternative<Reference>(content_); }

        Interface const& resolved_iface() const {
            if (auto* e = std::get_if<Embedded>(&content_)) {
                return e->inline_def->iface();
            }
            return std::get<Reference>(content_).resolved_iface;
        }

        ui::InternedId blueprint_id() const {
            if (auto* e = std::get_if<Embedded>(&content_)) {
                return e->blueprint_id;
            }
            return std::get<Reference>(content_).blueprint_id;
        }

        Blueprint const* inline_def() const {
            if (auto* e = std::get_if<Embedded>(&content_)) {
                return e->inline_def.get();
            }
            return nullptr;
        }

        Blueprint* inline_def_mut() {
            if (auto* e = std::get_if<Embedded>(&content_)) {
                return e->inline_def.get();
            }
            return nullptr;
        }

        // ── Mutators ──

        /// Replace the inline definition of an Embedded nested.
        /// Throws if this nested is not Embedded or if def is null.
        void set_inline_def(std::unique_ptr<Blueprint> def) {
            if (!def) {
                throw std::logic_error("set_inline_def requires non-null def");
            }
            std::get<Embedded>(content_).inline_def = std::move(def);
        }

        /// Convert this nested to Embedded mode with the given definition.
        void convert_to_embedded(ui::InternedId bp_id, std::unique_ptr<Blueprint> def) {
            if (!def) {
                throw std::logic_error("convert_to_embedded requires non-null def");
            }
            content_ = Embedded{bp_id, std::move(def)};
        }

        Nested() = delete;
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

    /// Return the nested instance hosted by this node when it is a composite host.
    /// The current canonical relation is host node id == nested instance id.
    Nested const* find_hosted_nested(Node const& node) const;
    bool is_embedded_proxy_node(Node const& node) const;

    /// Return the authoritative interface for a node.
    /// For composite host nodes, embedded nested authority wins.
    Interface const& effective_node_iface(ui::InternedId node_id) const;
    Interface const& effective_node_iface(Node const& node) const;

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
    void validate(::TypeRegistry const& parser_registry, ui::StringInterner& interner) const;
    void validate(::TypeRegistry const& parser_registry,
                  ui::StringInterner& interner,
                  PathArena const& arena) const;

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
