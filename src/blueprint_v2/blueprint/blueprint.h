#pragma once

#include "core/strings/interned_id.h"
#include "blueprint_v2/interface/interface.h"
#include "blueprint_v2/blueprint/node_color.h"
#include "blueprint_v2/path/path.h"
#include <variant>
#include <vector>
#include <unordered_map>
#include <optional>
#include <string>
#include <memory>
#include <utility>

struct ComponentRegistry;

namespace bp2 {

/// Position update for batch node position changes.
struct NodePositionUpdate {
    core::InternedId id;
    float x{};
    float y{};
};

class Blueprint {
public:
    struct NodeIfaceAuthority {
        core::StringInterner& interner;
        ::ComponentRegistry const* registry = nullptr;
    };

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
            core::InternedId id;
            core::InternedId type;
            std::unordered_map<core::InternedId, float> params;
            /// String-valued parameters (e.g. font_size, text content).
            /// Kept separate from numeric params to avoid stof() failures.
            std::unordered_map<std::string, std::string> string_params;

            bool operator==(SemanticData const& o) const {
                return id == o.id && type == o.type && params == o.params && string_params == o.string_params;
            }
        };

        struct BlueprintSource {
            struct Embedded {
                std::unique_ptr<Blueprint> blueprint;

                Embedded() = delete;
                explicit Embedded(std::unique_ptr<Blueprint> bp)
                    : blueprint(std::move(bp)) {}
                Embedded(const Embedded& other);
                Embedded(Embedded&&) noexcept = default;
                Embedded& operator=(const Embedded& other);
                Embedded& operator=(Embedded&&) noexcept = default;

                bool operator==(Embedded const& o) const;
            };

            struct Reference {
                /// Sole referenced authority for blueprint-instance sources.
                core::InternedId blueprint_id;
            };

            std::variant<Embedded, Reference> value;

            BlueprintSource() = delete;
            explicit BlueprintSource(Embedded embedded)
                : value(std::move(embedded)) {}
            explicit BlueprintSource(Reference reference)
                : value(reference) {}

            BlueprintSource(const BlueprintSource& other);
            BlueprintSource(BlueprintSource&&) noexcept = default;
            BlueprintSource& operator=(const BlueprintSource& other);
            BlueprintSource& operator=(BlueprintSource&&) noexcept = default;

            static BlueprintSource make_embedded(std::unique_ptr<Blueprint> blueprint);
            static BlueprintSource make_reference(core::InternedId blueprint_id);

            [[nodiscard]] bool is_embedded() const;
            [[nodiscard]] bool is_reference() const;
            [[nodiscard]] core::InternedId blueprint_id() const;
            [[nodiscard]] Blueprint const* inline_def() const;
            Blueprint* inline_def_mut();
            void set_inline_def(std::unique_ptr<Blueprint> blueprint);

            [[nodiscard]] bool canonical_eq(const BlueprintSource& other) const;
            bool operator==(const BlueprintSource& other) const;
        };

        struct ComponentData {
            Interface iface;

            bool operator==(ComponentData const& o) const {
                return iface == o.iface;
            }
        };

        struct BlueprintInstanceData {
            BlueprintSource source;

            bool operator==(BlueprintInstanceData const& o) const {
                return source == o.source;
            }
        };

        struct BridgePortData {
            core::InternedId exposed_port;
            bp2::BridgeDirection direction = bp2::BridgeDirection::Input;
            PortType port_type = PortType::Contextual;

            bool operator==(BridgePortData const& o) const {
                return exposed_port == o.exposed_port
                    && direction == o.direction
                    && port_type == o.port_type;
            }
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

        struct ViewData {
            /// Canonical authored node label persisted as JSON `label`.
            std::string name;
            std::optional<NodeColor> color;

            bool operator==(ViewData const& o) const {
                return name == o.name && color == o.color;
            }
        };

        SemanticData semantic;
        std::variant<ComponentData, BlueprintInstanceData, BridgePortData> content = ComponentData{};
        LayoutData layout;
        ViewData view;

        [[nodiscard]] bool is_component() const { return std::holds_alternative<ComponentData>(content); }
        [[nodiscard]] bool is_blueprint_instance() const { return std::holds_alternative<BlueprintInstanceData>(content); }
        [[nodiscard]] bool is_bridge_port() const { return std::holds_alternative<BridgePortData>(content); }
        [[nodiscard]] ComponentData const& component() const { return std::get<ComponentData>(content); }
        ComponentData& component() { return std::get<ComponentData>(content); }
        [[nodiscard]] BlueprintInstanceData const& blueprint_instance() const { return std::get<BlueprintInstanceData>(content); }
        BlueprintInstanceData& blueprint_instance() { return std::get<BlueprintInstanceData>(content); }
        [[nodiscard]] BridgePortData const& bridge_port() const { return std::get<BridgePortData>(content); }
        BridgePortData& bridge_port() { return std::get<BridgePortData>(content); }
        [[nodiscard]] bool has_embedded_blueprint() const {
            return is_blueprint_instance() && blueprint_instance().source.is_embedded();
        }
        [[nodiscard]] bool has_referenced_blueprint() const {
            return is_blueprint_instance() && blueprint_instance().source.is_reference();
        }

        [[nodiscard]] bool canonical_eq(Node const& o) const;
        bool operator==(Node const& o) const {
            return semantic == o.semantic && content == o.content && layout == o.layout && view == o.view;
        }
    };

    struct Wire {
        core::InternedId id;
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

    core::InternedId id() const { return id_; }
    std::string const& name() const { return name_; }
    Interface const& iface() const { return iface_; }

    std::vector<Node> const& nodes() const { return nodes_; }
    std::vector<Wire> const& wires() const { return wires_; }

    Node const* find_node(core::InternedId id) const;
    Wire const* find_wire(core::InternedId id) const;
    Node const* find_blueprint_instance(core::InternedId id) const;
    bool is_blueprint_instance_node(Node const& node) const;
    bool is_embedded_blueprint_instance(Node const& node) const;
    bool is_referenced_blueprint_instance(Node const& node) const;

    /// Resolve the authoritative interface for a node using explicit authority.
    /// Bridge ports require `interner`; referenced blueprint instances also
    /// require `registry`. Missing required authority throws.
    Interface resolve_node_iface(Node const& node,
                                 NodeIfaceAuthority authority) const;

    Blueprint with_node(Node n) const;
    Blueprint without_node(core::InternedId id) const;
    Blueprint with_wire(Wire w) const;
    Blueprint without_wire(core::InternedId id) const;
    /// Batch-remove a node and multiple wires in a single copy.
    /// More efficient than calling without_node + without_wire repeatedly.
    Blueprint without_node_and_wires(core::InternedId node_id,
                                      const std::vector<core::InternedId>& wire_ids) const;
    Blueprint with_id(core::InternedId id) const;
    Blueprint with_name(std::string n) const;
    Blueprint with_interface(Interface iface) const;
    Blueprint clone(core::InternedId new_id) const;

    /// Batch-update node positions in a single pass (O(N)).
    /// Returns a new blueprint with updated layout.x/layout.y for matching nodes.
    /// Nodes not in the update list are unchanged.
    Blueprint with_updated_positions(const std::vector<NodePositionUpdate>& updates) const;

    /// Returns all (path, port) pairs reachable from this blueprint.
    std::vector<std::pair<Path, PortDescriptor>> all_ports(PathArena& arena,
                                                           ::ComponentRegistry const& parser_registry,
                                                           core::StringInterner& interner) const;

    /// Validates all invariants. Throws std::runtime_error on failure.
    void validate(::ComponentRegistry const& parser_registry, core::StringInterner& interner) const;
    void validate(::ComponentRegistry const& parser_registry,
                  core::StringInterner& interner,
                  PathArena const& arena) const;

    bool canonical_eq(Blueprint const& other) const;
    bool operator==(Blueprint const& other) const;
    bool operator!=(Blueprint const& other) const { return !(*this == other); }

private:
    core::InternedId id_;
    std::string name_;
    Interface iface_;
    std::vector<Node> nodes_;
    std::vector<Wire> wires_;

    mutable std::unordered_map<core::InternedId, size_t> node_idx_;
    mutable bool node_idx_valid_ = false;
    mutable std::unordered_map<core::InternedId, size_t> wire_idx_;
    mutable bool wire_idx_valid_ = false;

    void ensure_node_index() const;
    void ensure_wire_index() const;

    void collect_ports_recursive(
        std::vector<std::pair<Path, PortDescriptor>>& result,
        PathArena& arena,
        Path prefix,
        ::ComponentRegistry const& parser_registry,
        core::StringInterner& interner) const;
};

} // namespace bp2
