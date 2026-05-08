# Blueprint V2 System

## Overview

Blueprint V2 is an immutable data model for representing hierarchical circuit blueprints. It uses copy-on-write semantics for cheap snapshots and undo/redo support.

## Core Classes

### Blueprint

```cpp
namespace bp2 {

class Blueprint {
public:
    struct Node {
        // Per-port layout overrides
        struct PortLayoutOverride {
            std::string port_name;
            std::optional<std::string> side;
            std::optional<int> position;
        };

        // Semantic/behavioral data
        struct SemanticData {
            core::InternedId id;
            core::InternedId type;
            std::unordered_map<core::InternedId, float> params;
            std::unordered_map<std::string, std::string> string_params;
        };

        struct BlueprintSource {
            struct Embedded {
                std::unique_ptr<Blueprint> blueprint;
            };
            struct Referenced {
                core::InternedId blueprint_id;
            };
            std::variant<Embedded, Referenced> source;
        };

        SemanticData semantic;
        std::optional<BlueprintSource> blueprint_source;
        std::vector<PortLayoutOverride> port_layout_overrides;

        float x = 0, y = 0;
        std::string name;
        std::string render_hint;
        std::string group_id;
        NodeColor color = NodeColor::Default;
        NodeContentType content_type = NodeContentType::None;
    };

    struct Wire {
        core::InternedId id;
        Path source;
        Path target;
        Domain domain = Domain::Electrical;
    };

    // Immutable operations (return new Blueprint)
    Blueprint with_node(Node n) const;
    Blueprint without_node(core::InternedId id) const;
    Blueprint with_wire(Wire w) const;
    Blueprint without_wire(core::InternedId id) const;

    // Accessors
    std::vector<Node> const& nodes() const;
    std::vector<Wire> const& wires() const;
};

}
```

### Interface & PortDescriptor

```cpp
struct PortDescriptor {
    core::InternedId name;
    Domain domain;
    PortDirection direction;  // Input, Output, Bidirectional
    PortType type;            // V, I, P, Q, T, H, Signal, Bool, RPM, Pressure, Position, Contextual, Any
};

class Interface {
    std::vector<PortDescriptor> ports_;
public:
    std::vector<PortDescriptor> const& ports() const;
    PortDescriptor const* find(core::InternedId name) const;
};
```

### Path

Hierarchical address to any element:
```
/                    → Root
/battery             → Node "battery"
/battery:v_out       → Port "v_out" on battery
/sub1/internal:v_in  → Nested path
```

```cpp
class Path {
    std::vector<core::InternedId> segments_;
    std::optional<core::InternedId> port_;
public:
    bool is_root() const;
    bool has_port() const;
    core::InternedId port() const;
    std::string to_string() const;
    static Path parse(std::string_view s, StringInterner& interner);
};
```

## ComponentRegistry

Registry of all component types and their interfaces:

```cpp
struct ComponentRegistry {
    void register_type(const std::string& classname, ComponentSpec spec,
                       TypePresentation pres = {}, std::string category = "");
    const ComponentSpec* get(const std::string& classname) const;
    bool has(const std::string& classname) const;
    std::vector<std::string> list_classnames() const;
};
```

File: `src/core/model/component_registry.h`

## EditorModel

Mutable editor state with undo/redo:

```cpp
class EditorModel {
public:
    Blueprint const& current() const;

    bool add_node(Blueprint::Node node);
    bool remove_node(core::InternedId id);
    bool add_wire(Blueprint::Wire wire);
    bool remove_wire(core::InternedId id);
    bool update_node(core::InternedId id, const std::function<void(Blueprint::Node&)>& fn);
    bool update_node_position(core::InternedId id, float x, float y);

    MutationResult mutate_embedded(std::span<const core::InternedId> path,
                                   const std::function<Blueprint(const Blueprint&)>& mutation);

    bool can_undo() const;
    bool can_redo() const;
    void undo();
    void redo();
    void push_checkpoint();
    bool mutate_atomically(const std::function<void()>& fn);

    bool is_dirty() const;
    void mark_saved();
};
```

File: `src/blueprint_v2/editor_model/editor_model.h`

## BlueprintCodec

Strict v1 persistence:

```cpp
class BlueprintCodec {
public:
    static std::string encode(Blueprint const& bp,
                              core::StringInterner const& interner,
                              PathArena const& arena,
                              const ::ComponentRegistry* parser_registry = nullptr);

    static std::optional<Blueprint> decode(
        std::string_view json,
        core::StringInterner& interner,
        PathArena& arena,
        const ::ComponentRegistry& parser_registry,
        DecodeError* error_out = nullptr);
};
```

File: `src/blueprint_v2/codec/blueprint_codec.h`

## Flattener

Resolves nested blueprints into a flat device list:

```cpp
std::vector<ResolvedDevice> flatten(const Blueprint& bp,
                                    const ComponentRegistry& registry,
                                    const bp2::LibraryIndex* library_index = nullptr);
```

File: `src/blueprint_v2/flattener/flattener.h`

## Validation

- `src/blueprint_v2/validation/wire_validator.h` — Wire domain/direction validation
- `src/blueprint_v2/validation/path_resolver.h` — Path resolution
- `src/blueprint_v2/validation/signal_typing.h` — Signal type checking
- `src/blueprint_v2/validation/invariant_checker.h` — Structural invariants
- `src/blueprint_v2/diagnostics/repair.h` — Auto-repair suggestions

## Files

| File | Purpose |
|------|---------|
| `src/blueprint_v2/blueprint/blueprint.h` | Blueprint class |
| `src/blueprint_v2/blueprint/blueprint_replace.h` | Immutable replacement ops |
| `src/blueprint_v2/blueprint/canonicalize.h` | Canonicalization |
| `src/blueprint_v2/interface/interface.h` | Interface, PortDescriptor |
| `src/blueprint_v2/path/path.h` | Path class |
| `src/blueprint_v2/editor_model/editor_model.h` | EditorModel |
| `src/blueprint_v2/codec/blueprint_codec.h` | BlueprintCodec |
| `src/blueprint_v2/flattener/flattener.h` | Flattener |
| `src/blueprint_v2/validation/` | Validation suite |
| `src/blueprint_v2/diagnostics/repair.h` | Auto-repair |
| `src/blueprint_v2/layout/auto_layout.h` | Auto-layout (Sugiyama) |
| `src/blueprint_v2/library/library_index.h` | LibraryIndex |
