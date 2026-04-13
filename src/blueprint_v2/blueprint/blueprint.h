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
        enum class Kind {
            Component,
            BlueprintInstance,
        };

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
            /// Component-node interface cache.
            ///
            /// Contract (#108):
            /// - component nodes: authoritative and required
            /// - blueprint-instance nodes: must be empty
            ///
            /// For blueprint-instance nodes, always use
            /// `Blueprint::effective_node_iface()` to read authoritative
            /// interface data from `source`.
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

        struct BlueprintSource {
            struct Embedded {
                ui::InternedId blueprint_id;
                std::unique_ptr<Blueprint> blueprint;

                Embedded() = delete;
                Embedded(ui::InternedId bp_id, std::unique_ptr<Blueprint> bp)
                    : blueprint_id(bp_id), blueprint(std::move(bp)) {}
                Embedded(const Embedded& other);
                Embedded(Embedded&&) noexcept = default;
                Embedded& operator=(const Embedded& other);
                Embedded& operator=(Embedded&&) noexcept = default;
            };

            struct Reference {
                /// Sole referenced authority for blueprint-instance sources.
                ui::InternedId blueprint_id;
                /// Strictly derived in-memory cache populated from authoritative
                /// referenced blueprint/type resolution. Must validate against
                /// the registry; must never be treated as independent authority.
                Interface cached_iface;
            };

            std::variant<Embedded, Reference> value;

            BlueprintSource() = delete;
            explicit BlueprintSource(Embedded embedded)
                : value(std::move(embedded)) {}
            explicit BlueprintSource(Reference reference)
                : value(std::move(reference)) {}

            BlueprintSource(const BlueprintSource& other);
            BlueprintSource(BlueprintSource&&) noexcept = default;
            BlueprintSource& operator=(const BlueprintSource& other);
            BlueprintSource& operator=(BlueprintSource&&) noexcept = default;

            static BlueprintSource make_embedded(ui::InternedId blueprint_id,
                                                 std::unique_ptr<Blueprint> blueprint);
            static BlueprintSource make_reference(ui::InternedId blueprint_id,
                                                 Interface cached_iface);

            bool is_embedded() const;
            bool is_reference() const;
            ui::InternedId blueprint_id() const;
            Interface const& cached_iface() const;
            Blueprint const* inline_def() const;
            Blueprint* inline_def_mut();
            void set_inline_def(std::unique_ptr<Blueprint> blueprint);

            bool canonical_eq(const BlueprintSource& other) const;
            bool operator==(const BlueprintSource& other) const;
        };

        // === Layout/positioning data ===
        struct LayoutData {
            float x = 0.0f;
            float y = 0.0f;
            bool collapsed = true;
            bool manual_size = false;
            std::optional<float> width;
            std::optional<float> height;
            std::vector<PortLayoutOverride> layout_overrides;

            bool operator==(LayoutData const& o) const {
                return x == o.x && y == o.y && collapsed == o.collapsed
                    && manual_size == o.manual_size
                    && width == o.width && height == o.height
                    && layout_overrides == o.layout_overrides;
            }
        };

        // === View/presentation data ===
        //
        // Design decision (#107): ViewData intentionally lives inside the
        // core Blueprint::Node model as a pragmatic superset of canonical
        // persistence authority.  The fields are grouped into three tiers:
        //
        //   1. **Canonical** — authored document state, persisted in strict
        //      blueprint v1 JSON (e.g. `name` → JSON `label`).
        //
        //   2. **Runtime/editor hydrated** — populated by an explicit
        //      post-load hydration step from the TypeRegistry.  NOT
        //      persisted; must never be serialized.  Hydration is owned
        //      exclusively by `editor::hydrate_runtime_node_view_data()`.
        //      Fields: render_hint, content_*.
        //
        //   3. **Session/editor-only** — transient visual state that lives
        //      only in the running editor session.  NOT persisted; NOT
        //      hydrated from TypeRegistry.  Fields: has_color, color_*.
        //
        // `canonical_eq()` compares only tier-1 fields and should be used
        // for persistence dirty-checking.  `operator==` compares all tiers
        // and is used for structural equality in tests and model diffing.
        //
        struct ViewData {
            // --- Tier 1: Canonical authored state (persisted) ---
            /// Canonical authored node label persisted as JSON `label`.
            std::string name;

            // --- Tier 2: Runtime/editor hydrated state (NOT persisted) ---
            // Populated exclusively by editor::hydrate_runtime_node_view_data().
            // Must NOT be set by BlueprintCodec::decode().
            std::string render_hint;
            NodeContentType content_type = NodeContentType::None;
            std::string content_label;
            float content_value = 0.0f;
            float content_min = 0.0f;
            float content_max = 1.0f;
            std::string content_unit;
            bool content_state = false;
            bool content_tripped = false;

            // --- Tier 3: Session/editor-only state (NOT persisted, NOT hydrated) ---
            // Per-node custom color (has_color=false means use theme default).
            bool has_color = false;
            float color_r = 0.5f, color_g = 0.5f, color_b = 0.5f, color_a = 1.0f;

            /// Compare only canonical persisted fields (tier 1).
            bool canonical_eq(ViewData const& o) const {
                return name == o.name;
            }

            /// Full structural equality across all tiers.
            bool operator==(ViewData const& o) const {
                return name == o.name && render_hint == o.render_hint
                    && content_type == o.content_type && content_label == o.content_label
                    && content_value == o.content_value && content_min == o.content_min
                    && content_max == o.content_max && content_unit == o.content_unit
                    && content_state == o.content_state && content_tripped == o.content_tripped
                    && has_color == o.has_color && color_r == o.color_r && color_g == o.color_g
                    && color_b == o.color_b && color_a == o.color_a;
            }
        };

        Kind kind = Kind::Component;
        SemanticData semantic;
        std::optional<BlueprintSource> source;
        LayoutData layout;
        ViewData view;

        bool is_component() const { return kind == Kind::Component; }
        bool is_blueprint_instance() const { return kind == Kind::BlueprintInstance; }
        bool has_embedded_blueprint() const {
            return source.has_value() && source->is_embedded();
        }
        bool has_referenced_blueprint() const {
            return source.has_value() && source->is_reference();
        }

        bool canonical_eq(Node const& o) const;
        bool operator==(Node const& o) const {
            return kind == o.kind && semantic == o.semantic && source == o.source
                && layout == o.layout && view == o.view;
        }
    };

    struct Wire {
        ui::InternedId id;
        WireEndpoint source;
        WireEndpoint target;
        Domain domain = Domain::Electrical;
        std::vector<std::pair<float,float>> routing_points;

        bool operator==(Wire const& o) const {
            return id == o.id && source == o.source
                && target == o.target && domain == o.domain
                && routing_points == o.routing_points;
        }
    };

    Blueprint() = default;
    Blueprint(Blueprint const& other);
    Blueprint(Blueprint&& other) noexcept;
    Blueprint& operator=(Blueprint const& other);
    Blueprint& operator=(Blueprint&& other) noexcept;

    ui::InternedId id() const { return id_; }
    std::string const& name() const { return name_; }
    Interface const& iface() const { return iface_; }

    std::vector<Node> const& nodes() const { return nodes_; }
    std::vector<Wire> const& wires() const { return wires_; }

    Node const* find_node(ui::InternedId id) const;
    Wire const* find_wire(ui::InternedId id) const;
    Node const* find_blueprint_instance(ui::InternedId id) const;
    bool is_blueprint_instance_node(Node const& node) const;
    bool is_embedded_blueprint_instance(Node const& node) const;
    bool is_referenced_blueprint_instance(Node const& node) const;

    /// Return the authoritative interface for a node.
    /// For blueprint-instance nodes, source authority wins:
    /// - embedded sources: inline blueprint interface
    /// - reference sources: cache derived from authoritative blueprint_id
    ///   and validated against the registry
    Interface const& effective_node_iface(ui::InternedId node_id) const;
    Interface const& effective_node_iface(Node const& node) const;

    Blueprint with_node(Node n) const;
    Blueprint without_node(ui::InternedId id) const;
    Blueprint with_wire(Wire w) const;
    Blueprint without_wire(ui::InternedId id) const;
    Blueprint with_id(ui::InternedId id) const;
    Blueprint with_name(std::string n) const;
    Blueprint with_interface(Interface iface) const;
    Blueprint clone(ui::InternedId new_id) const;

    /// Returns all (path, port) pairs reachable from this blueprint.
    std::vector<std::pair<Path, PortDescriptor>> all_ports(PathArena& arena) const;

    /// Validates all invariants. Throws std::runtime_error on failure.
    void validate(::TypeRegistry const& parser_registry, ui::StringInterner& interner) const;
    void validate(::TypeRegistry const& parser_registry,
                  ui::StringInterner& interner,
                  PathArena const& arena) const;

    bool canonical_eq(Blueprint const& other) const;
    bool operator==(Blueprint const& other) const;
    bool operator!=(Blueprint const& other) const { return !(*this == other); }

private:
    ui::InternedId id_;
    std::string name_;
    Interface iface_;
    std::vector<Node> nodes_;
    std::vector<Wire> wires_;

    mutable std::unordered_map<ui::InternedId, size_t> node_idx_;
    mutable bool node_idx_valid_ = false;
    mutable std::unordered_map<ui::InternedId, size_t> wire_idx_;
    mutable bool wire_idx_valid_ = false;

    void ensure_node_index() const;
    void ensure_wire_index() const;

    void collect_ports_recursive(
        std::vector<std::pair<Path, PortDescriptor>>& result,
        PathArena& arena,
        Path prefix) const;
};

} // namespace bp2
