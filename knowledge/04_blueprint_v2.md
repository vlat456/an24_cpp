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
        ui::InternedId id;
        ui::InternedId type;
        Interface iface;
        std::unordered_map<ui::InternedId, float> params;
        float x = 0, y = 0;
        std::string name, render_hint, group_id;
        NodeContentType content_type = NodeContentType::None;
        // ... visual properties
    };
    
    struct Wire {
        ui::InternedId id;
        Path source;  // e.g., "/battery:v_out"
        Path target;  // e.g., "/load:v_in"
        Domain domain = Domain::Electrical;
    };
    
    struct Nested {
        ui::InternedId id;
        ui::InternedId blueprint_id;
        std::unique_ptr<Blueprint> inline_def;
        Interface iface;
    };
    
    // Immutable operations (return new Blueprint)
    Blueprint with_node(Node n) const;
    Blueprint without_node(ui::InternedId id) const;
    Blueprint with_wire(Wire w) const;
    Blueprint without_wire(ui::InternedId id) const;
    Blueprint with_nested(Nested n) const;
    
    // Accessors
    std::vector<Node> const& nodes() const;
    std::vector<Wire> const& wires() const;
    std::vector<Nested> const& nested() const;
};

}
```

### Interface & PortDescriptor

```cpp
struct PortDescriptor {
    ui::InternedId name;
    Domain domain;
    PortDirection direction;  // Input, Output, Bidirectional
    PortType type;            // V, I, P, Q, T, H, Signal
};

class Interface {
    std::vector<PortDescriptor> ports_;
public:
    std::vector<PortDescriptor> const& ports() const;
    PortDescriptor const* find(ui::InternedId name) const;
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
    std::vector<ui::InternedId> segments_;
    std::optional<ui::InternedId> port_;
public:
    bool is_root() const;
    bool has_port() const;
    ui::InternedId port() const;
    std::string to_string() const;
    static Path parse(std::string_view s, StringInterner& interner);
};
```

## TypeRegistry

Registry of all component types and their interfaces:

```cpp
class TypeRegistry {
    std::unordered_map<ui::InternedId, TypeDefinition> types_;
public:
    void register_type(TypeDefinition def);
    TypeDefinition const* lookup(ui::InternedId id) const;
    Interface const* get_interface(ui::InternedId type_id) const;
};
```

### TypeDefinition

```cpp
struct TypeDefinition {
    ui::InternedId id;
    std::string display_name;
    Interface iface;
    bool cpp_class;  // Has C++ implementation?
    
    // For composites only:
    std::vector<Node> nodes;
    std::vector<Wire> wires;
};
```

## Flattener

Converts hierarchical blueprints to flat netlist:

```cpp
class Flattener {
public:
    Flattener(const BlueprintLibrary& library);
    FlatNetlist flatten(const Blueprint& bp, PathArena& arena);
};
```

### FlatNetlist

```cpp
struct FlatNetlist {
    struct Component {
        Path path;                          // Hierarchical path to this component
        ui::InternedId type;                // Component classname
        std::vector<PortDescriptor> ports;  // Resolved port descriptors
        std::map<ui::InternedId, float> params;
        std::map<std::string, std::string> string_params;
        std::map<ui::InternedId, uint32_t> port_signals; // port → signal index
        ui::InternedId exposed_port_name;   // Non-empty for bridge nodes
    };
    
    std::vector<Component> components;
    // Signal allocation done by compact_signals() using UnionFind
};
```

## Elaboration Layer

Converts FlatNetlist to runtime-ready build inputs. Two paths sharing core logic:

```
FlatNetlist ──┬─→ elaborate_for_jit()     → JitBuildInput (InternedId keys)
              └─→ elaborate_for_codegen() → CodegenBuildInput (string keys)
```

### Shared Infrastructure

- `elaboration_utils.h` — Lightweight shared utils (no jit_solver.h dependency):
  - `node_id_from_path()` — Path → colon-separated node_id string
  - `exposed_key_for_bridge()` — Bridge parent-facing signal key
- `elaboration_detail.h` — Shared device-building logic:
  - `build_resolved_device()` — Per-component ResolvedDevice builder
  - `collect_devices()` — Phase 1: device list from FlatNetlist
  - `collect_port_signals()` — Phase 2: port-to-signal map

### JIT Path (`sim_export.h`)

```cpp
JitBuildInput elaborate_for_jit(
    const FlatNetlist& netlist,
    PathArena& arena,
    const ui::StringInterner& interner,
    const ComponentRegistry& type_registry);
```

### Codegen Path (`codegen_export.h`)

```cpp
struct CodegenBuildInput {
    std::vector<ResolvedDevice> devices;
    std::unordered_map<std::string, uint32_t> port_to_signal; // string keys
    uint32_t signal_count = 0;
};

CodegenBuildInput elaborate_for_codegen(
    const FlatNetlist& netlist,
    PathArena& arena,
    const ui::StringInterner& interner,
    const ComponentRegistry& type_registry);
```

## EditorModel

Wraps Blueprint with undo/redo and dirty tracking:

```cpp
class EditorModel {
    Blueprint blueprint_;
    std::vector<Blueprint> undo_stack_;
    std::vector<Blueprint> redo_stack_;
    bool dirty_ = false;
    
public:
    Blueprint const& blueprint() const;
    void execute(Command cmd);  // Mutates and pushes undo
    bool can_undo() const;
    void undo();
    void redo();
    bool dirty() const;
    void mark_clean();
};
```

## Validation

### InvariantChecker
Validates blueprint invariants:
- All wire endpoints exist
- Domain compatibility
- No duplicate IDs
- No cycles in nested hierarchy

### WireValidator
Checks wire connections:
- Port exists
- Direction compatibility
- Domain match

## Commands

All mutations go through typed commands:

```cpp
using Command = std::variant<
    CmdAddNode,
    CmdRemoveNode,
    CmdMoveNode,
    CmdAddWire,
    CmdRemoveWire,
    CmdSetParam,
    CmdSetNodeName,
    // ...
>;

void execute(EditorModel& model, StringInterner& interner, Command cmd);
```

## Bake/Unbake

### Bake
Expands nested blueprint inline:
- Replaces `Nested` reference with actual nodes/wires
- Rewires connections through interface ports

### Unbake
Collapses expanded nodes back to nested reference:
- Detects matching patterns
- Creates `Nested` instance
