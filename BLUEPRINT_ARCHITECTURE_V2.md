# Blueprint Architecture v2: A First-Principles Redesign

> This document proposes a clean-slate replacement for the current blueprint/sub-blueprint/flattening/wire-renaming system. The current system has accumulated significant technical debt across three competing representations (`Blueprint`, `FlatBlueprint`, `TypeDefinition`), string-based hierarchical scoping via raw `"parent:child"` concatenation, duplicated serialization paths, and fragile bake-in logic. This redesign eliminates all of that.

---

## Part I: Design Philosophy

### The Four Pillars

```
+------------------------------------------------------------------+
|  1. SINGLE SOURCE OF TRUTH                                        |
|     One canonical Blueprint type. All other views are derived.    |
+------------------------------------------------------------------+
|  2. TYPED HIERARCHY                                               |
|     Paths are algebraic types, not strings. Parse once, use       |
|     everywhere. Ambiguity is impossible by construction.          |
+------------------------------------------------------------------+
|  3. PURE TRANSFORMATIONS                                          |
|     Flattening, serialization, and indexing are pure functions    |
|     with no side effects. Input -> Output, always deterministic.  |
+------------------------------------------------------------------+
|  4. EXPLICIT CONTRACTS                                            |
|     Interfaces are declared, not inferred. Validation happens     |
|     at construction time, not at runtime.                         |
+------------------------------------------------------------------+
```

### Core Insight: Blueprints Are Values

A `Blueprint` is an immutable value type, like a mathematical expression. It can be:
- **Compared** for equality
- **Hashed** for caching
- **Copied** freely without aliasing bugs
- **Transformed** into other forms without mutation

The editor mutates a `Blueprint` by replacing it with a new value (command pattern). This is how Undo/Redo becomes trivial.

### Current System Problems (for reference)

| # | Problem | Impact |
|---|---------|--------|
| 1 | Three competing representations (`Blueprint`, `FlatBlueprint`, `TypeDefinition`) | Every feature requires changes in 3 places + 2 bridges |
| 2 | String-based `"parent:child"` scoping | No validation, ambiguity if IDs contain `:` |
| 3 | Two divergent serialization paths (`to_flat` and `to_simulator_json`) | Same wire-rewriting implemented twice, differently |
| 4 | Copy-paste duplication in node deserialization | Changes must be made in 2 places |
| 5 | Implicit "Option B" rename of BlueprintInput/Output | Silent rename that simulator depends on but can't verify |
| 6 | Fragile bake-in | Complex override application, no un-bake, no nested bake-in |
| 7 | Static TypeRegistry | Function-local static, not injectable, never refreshes |
| 8 | 5 manually-maintained indices | Easy to get stale, each a source of bugs |

---

## Part II: Core Type Definitions

### 2.1 Identity: Interned Strings

```cpp
/// An interned string: unique pointer in a global pool.
/// Comparison is pointer equality (O(1)). Hash is the pointer itself.
/// Created only through SymbolTable::intern().
class InternedId {
    const char* ptr_;  // Points into immortal arena

public:
    constexpr InternedId() : ptr_(nullptr) {}

    // Pointer equality semantics
    bool operator==(InternedId other) const { return ptr_ == other.ptr_; }
    bool operator!=(InternedId other) const { return ptr_ != other.ptr_; }
    bool operator<(InternedId other) const { return ptr_ < other.ptr_; }

    std::string_view view() const { return ptr_ ? std::string_view(ptr_) : ""; }
    explicit operator bool() const { return ptr_ != nullptr; }

    // Hash is just the pointer value
    size_t hash() const { return reinterpret_cast<size_t>(ptr_); }
};

/// Global symbol table. Thread-safe for interning, read-only access is lock-free.
class SymbolTable {
    static std::shared_mutex mutex_;
    static std::unordered_set<std::string> strings_;

public:
    static InternedId intern(std::string_view s) {
        std::unique_lock lock(mutex_);
        auto [it, _] = strings_.emplace(s);
        return InternedId{it->c_str()};
    }

    static InternedId intern(const char* s) { return intern(std::string_view(s)); }
};
```

### 2.2 Hierarchical Paths: Typed Algebraic Type

```cpp
/// A path through the blueprint hierarchy.
///
/// Examples:
///   Root{}                      -> the blueprint itself
///   Node{Root{}, "battery1"}    -> node "battery1" in root blueprint
///   Port{Node{...}, "v_out"}    -> port "v_out" on that node
///   Nested{Root{}, "sub1"}      -> sub-blueprint instance "sub1"
///   Node{Nested{Root{}, "sub1"}, "resistor1"}  -> node inside sub-blueprint
///
/// INVARIANT: A Path is always well-formed. Construction validates.
/// INVARIANT: String representation uses '/' separator and is parseable.
class Path {
public:
    enum class Kind : uint8_t { Root, Node, Port, Nested, Wire };

private:
    Kind kind_;
    InternedId segment_;  // The local name (node id, port name, etc.)
    uint32_t parent_idx_; // Index into a PathArena (see below)

    Path(Kind k, InternedId seg, uint32_t parent)
        : kind_(k), segment_(seg), parent_idx_(parent) {}

public:
    // === Constructors ===
    static Path root() { return Path{Kind::Root, {}, 0}; }

    static Path node(Path parent, InternedId node_id);
    static Path port(Path parent, InternedId port_name);
    static Path nested(Path parent, InternedId instance_id);
    static Path wire(Path parent, InternedId wire_id);

    // === Accessors ===
    Kind kind() const { return kind_; }
    InternedId segment() const { return segment_; }
    Path parent(PathArena const& arena) const;

    // === Rendering ===
    std::string to_string() const;      // "/sub1/battery1/v_out"
    static std::optional<Path> parse(std::string_view);  // Inverse

    // === Comparison ===
    bool operator==(Path other) const {
        return kind_ == other.kind_ && segment_ == other.segment_
               && parent_idx_ == other.parent_idx_;
    }
};

/// Arena for path interning. Paths are 8 bytes, parent is an index.
/// Enables O(1) path comparison and O(1) hashing.
class PathArena {
    std::vector<Path> paths_;  // Index 0 is always Root

public:
    Path root() const { return Path::root(); }

    Path make_node(Path parent, InternedId id) {
        uint32_t idx = paths_.size();
        paths_.push_back(Path{Path::Kind::Node, id, parent.parent_idx_});
        return paths_.back();
    }
    // ... similar for port, nested, wire
};
```

**Why this design?**
- Paths are **8 bytes** (kind + segment + index)
- Comparison is **O(1)** (just compare 8 bytes)
- No string parsing at runtime
- Type-safe: you can't construct an invalid path
- Renderable to string for serialization/debugging

### 2.3 Ports: The Interface Contract

```cpp
/// A port is a typed connection point on a node or blueprint.
struct PortDescriptor {
    InternedId name;          // "v_out", "ground", "rpm"
    Domain domain;            // Electrical, Mechanical, etc.
    Direction direction;      // Input, Output, InOut

    void validate() const {
        assert(name && "Port must have a name");
        assert(domain != Domain::Invalid);
    }
};

/// An interface is a set of ports. Blueprints and Components both have interfaces.
class Interface {
    std::vector<PortDescriptor> ports_;
    std::unordered_map<InternedId, size_t> name_to_idx_;

public:
    Interface() = default;
    explicit Interface(std::vector<PortDescriptor> ports);

    // Lookup by name
    std::optional<PortDescriptor> find(InternedId name) const;
    PortDescriptor const& at(InternedId name) const;
    bool has(InternedId name) const { return name_to_idx_.count(name); }

    // Iteration
    auto begin() const { return ports_.begin(); }
    auto end() const { return ports_.end(); }
    size_t size() const { return ports_.size(); }

    // For serialization
    std::vector<PortDescriptor> const& ports() const { return ports_; }
};
```

### 2.4 The Canonical Blueprint

```cpp
/// The single source of truth. A Blueprint is:
/// - A set of nodes (component instances)
/// - A set of wires (connections between ports)
/// - A set of nested blueprint instances (hierarchy)
/// - An interface (exposed ports)
///
/// INVARIANTS:
/// - All node IDs are unique within this blueprint
/// - All wire IDs are unique within this blueprint
/// - All nested instance IDs are unique within this blueprint
/// - All wire endpoints reference valid ports
/// - The interface ports are a subset of all available ports
class Blueprint {
public:
    // === Value Types ===

    struct Node {
        InternedId id;           // Unique within this blueprint
        InternedId type;         // Component type (from registry)
        Interface iface;         // Resolved interface (cached from registry)
        std::unordered_map<InternedId, float> params;  // Instance parameters
        float x, y;              // Editor position

        void validate() const;
    };

    struct Wire {
        InternedId id;           // Unique within this blueprint
        Path source;             // Path to source port
        Path target;             // Path to target port
        Domain domain;           // Resolved from source/target

        void validate() const;
    };

    struct Nested {
        InternedId id;           // Instance ID (unique in this blueprint)
        InternedId blueprint_id; // Which blueprint to instantiate
        bool embedded;           // true = local copy, false = reference
        std::optional<Blueprint> inline_def;  // If embedded, the actual blueprint
        Interface iface;         // Resolved interface (from blueprint or inline_def)
        float x, y;              // Editor position

        void validate() const;
    };

private:
    // === Core Data (ordered for deterministic serialization) ===

    InternedId id_;                        // Blueprint identity
    std::string display_name_;             // Human-readable name

    std::vector<Node> nodes_;              // Ordered by id
    std::vector<Wire> wires_;              // Ordered by id
    std::vector<Nested> nested_;           // Ordered by id

    Interface iface_;                      // Ports this blueprint exposes

    // === Indices (derived, lazy) ===
    mutable std::unordered_map<InternedId, size_t> node_idx_;
    mutable std::unordered_map<InternedId, size_t> wire_idx_;
    mutable std::unordered_map<InternedId, size_t> nested_idx_;
    mutable bool indexed_ = false;

    void ensure_index() const;

public:
    // === Construction ===

    Blueprint() = default;
    explicit Blueprint(InternedId id) : id_(id) {}

    // === Accessors ===

    InternedId id() const { return id_; }
    std::string const& display_name() const { return display_name_; }
    Interface const& iface() const { return iface_; }

    // === Container Access ===

    // By-index iteration (deterministic order)
    auto nodes() const -> std::vector<Node> const& { return nodes_; }
    auto wires() const -> std::vector<Wire> const& { return wires_; }
    auto nested() const -> std::vector<Nested> const& { return nested_; }

    // By-ID lookup (O(1) after first access)
    Node const* find_node(InternedId id) const;
    Wire const* find_wire(InternedId id) const;
    Nested const* find_nested(InternedId id) const;

    // === Mutation (returns new Blueprint, command pattern) ===

    Blueprint with_node(Node n) const;
    Blueprint without_node(InternedId id) const;
    Blueprint with_wire(Wire w) const;
    Blueprint without_wire(InternedId id) const;
    Blueprint with_nested(Nested n) const;
    Blueprint without_nested(InternedId id) const;
    Blueprint with_interface(Interface i) const;
    Blueprint with_display_name(std::string name) const;

    // === Validation ===

    /// Validates structural/type invariants. Throws on failure.
    /// NOTE: This overload does not validate wire Path endpoint resolution,
    /// because Path values are arena-relative.
    void validate(TypeRegistry const& registry) const;

    /// Full validation including wire Path endpoint resolution using the caller's
    /// PathArena context.
    void validate(TypeRegistry const& registry, PathArena const& arena) const;

    // === Operations ===

    /// Deep copy with new ID. Used for bake-in.
    Blueprint clone(InternedId new_id) const;

    /// All ports reachable from this blueprint's scope.
    std::vector<std::pair<Path, PortDescriptor>> all_ports(PathArena& arena) const;

    // === Comparison ===

    bool operator==(Blueprint const& other) const;
    size_t hash() const;
};
```

**Key Design Decisions:**

1. **Ordered vectors** for deterministic serialization and iteration
2. **Lazy indices** built on first lookup, invalidated on mutation
3. **Immutable mutations** return new Blueprint (enables undo/redo)
4. **Interface cached** on nested instances (avoids repeated registry lookups)
5. **Embedded blueprints stored inline** (self-contained for serialization)

### 2.5 Type Registry

```cpp
/// Registry of component types and blueprint definitions.
/// Provides interface resolution for any type.
class TypeRegistry {
public:
    struct Entry {
        InternedId type_id;
        Interface iface;
        std::string description;
        bool is_blueprint;      // false = built-in C++ component
    };

private:
    std::unordered_map<InternedId, Entry> entries_;
    std::function<void(InternedId)> on_missing_;  // Hook for lazy loading

public:
    // === Registration ===

    void register_component(InternedId type_id, Interface iface,
                           std::string description = "");
    void register_blueprint(InternedId type_id, Blueprint const& bp,
                           std::string description = "");

    // === Lookup ===

    std::optional<Entry const*> find(InternedId type_id) const;
    Entry const& at(InternedId type_id) const;
    bool has(InternedId type_id) const;

    // === Interface Resolution ===

    Interface const& interface_of(InternedId type_id) const;

    // === Dependency Injection ===

    void set_on_missing(std::function<void(InternedId)> callback) {
        on_missing_ = std::move(callback);
    }

    // === Factory for Testing ===

    static TypeRegistry create_test_registry();
};
```

**Why injectable?** Tests can create isolated registries with known state. No hidden globals.

---

## Part III: Hierarchical Addressing

### 3.1 Path Semantics

```
Path Grammar:
  path     ::= '/' segments
  segments ::= segment ('/' segment)*
  segment  ::= node_id | node_id ':' port_name | instance_id

Examples:
  /                                    -> Root blueprint
  /battery1                            -> Node "battery1" in root
  /battery1:v_out                      -> Port "v_out" on that node
  /sub_circuit1                        -> Nested instance "sub_circuit1"
  /sub_circuit1/resistor1              -> Node inside nested blueprint
  /sub_circuit1/resistor1:in           -> Port on that nested node
```

### 3.2 Wire Endpoint Resolution

```cpp
/// Resolves a path to its concrete port, traversing hierarchy.
/// Returns: (port_descriptor, owning_blueprint_path, is_boundary_port)
struct ResolvedPort {
    PortDescriptor port;
    Path blueprint_path;      // Path to the blueprint containing this port
    bool is_boundary;         // True if this is a blueprint's interface port
};

class PathResolver {
public:
    /// Resolve a path starting from root blueprint.
    std::optional<ResolvedPort> resolve(
        Path const& path,
        Blueprint const& root,
        TypeRegistry const& registry
    ) const;

    /// Check if two paths refer to connectable ports.
    bool can_connect(
        Path const& source,
        Path const& target,
        Blueprint const& root,
        TypeRegistry const& registry
    ) const;
};
```

### 3.3 Cross-Boundary Connection Rules

```
Valid Connections:
  +-------------------------------------------------------------+
  |  SOURCE                     TARGET                           |
  +-------------------------------------------------------------+
  |  /node1:out        ->  /node2:in          (same level)       |
  |  /node1:out        ->  /sub:input_port    (into sub)         |
  |  /sub:output_port  ->  /node2:in          (out of sub)       |
  |  /sub1:out         ->  /sub2:in           (sub to sub)       |
  |  /sub/node:out     ->  /sub:iface_out     (inner to iface)   |
  |  /sub:iface_in     ->  /sub/node:in       (iface to inner)   |
  +-------------------------------------------------------------+

Invalid Connections:
  +-------------------------------------------------------------+
  |  /node1:out        ->  /sub/node:in         (skips boundary) |
  |  /sub1/node:out    ->  /sub2/node:in        (different subs) |
  |  /node1:out        ->  /node1:in            (self-loop)      |
  |  /node1:elec_out   ->  /node2:mech_in       (domain mismatch)|
  +-------------------------------------------------------------+
```

**The Boundary Rule:** A wire must cross at most one blueprint boundary. To connect across multiple levels, each intermediate blueprint must have interface ports that forward the connection.

---

## Part IV: Flattening Algorithm

### 4.1 The Flatten Function

```cpp
/// Output of flattening: a flat netlist ready for the solver.
struct FlatNetlist {
    struct Component {
        Path path;                   // Original hierarchical path
        InternedId type;             // Component type
        std::unordered_map<InternedId, float> params;
        std::vector<std::pair<InternedId, SignalIndex>> port_signals;
    };

    struct Connection {
        SignalIndex signal;
        std::vector<Path> ports;     // All ports connected to this signal
        Domain domain;
    };

    std::vector<Component> components;
    std::vector<Connection> connections;
    std::unordered_map<Path, SignalIndex> port_to_signal;
};

/// Pure functional flattening. No side effects, deterministic output.
class Flattener {
    TypeRegistry const& registry_;
    PathArena arena_;

public:
    explicit Flattener(TypeRegistry const& reg) : registry_(reg) {}

    /// Main entry point. Flattens a blueprint tree into a netlist.
    FlatNetlist flatten(Blueprint const& root);

private:
    /// Recursive helper. Visits a blueprint, assigns signals, emits components.
    void visit_blueprint(
        Blueprint const& bp,
        Path prefix,
        std::unordered_map<Path, SignalIndex>& boundary_signals,
        FlatNetlist& out
    );

    /// Assigns a signal index to a port, merging with existing if connected.
    SignalIndex assign_signal(
        Path const& port_path,
        Domain domain,
        std::unordered_map<Path, SignalIndex>& signals,
        FlatNetlist& out
    );

    /// Merges two signals (when a wire connects them).
    void merge_signals(
        SignalIndex a,
        SignalIndex b,
        std::unordered_map<Path, SignalIndex>& signals,
        FlatNetlist& out
    );
};
```

### 4.2 Algorithm Walkthrough

```
Input: Blueprint with nested sub-blueprint

     Root Blueprint "main"
     +----------------------------------------+
     |  [Battery]--[Wire]--[SubCircuit]       |
     |     b1       w1         sub1           |
     |    v_out------------------>in_v        |
     |                    out_v-->[Wire]-[LED]|
     |                      w2      l1        |
     +----------------------------------------+
            |
            v
     SubCircuit Blueprint (embedded in sub1)
     +----------------------------------------+
     |  Interface: in_v (Input), out_v (Out)  |
     |                                        |
     |  [Resistor]--[Wire]--[Capacitor]       |
     |     r1        w3         c1            |
     |    in-------------------->in           |
     |   (in_v)            out---------->(out_v)
     +----------------------------------------+

Flattening Steps:

1. Visit root blueprint
   - Create signal for /b1:v_out (Signal 0, Electrical)
   - Visit node b1, emit Component{path=/b1, type=Battery}
     - Bind port v_out -> Signal 0

2. Visit nested sub1
   - Map interface port in_v to incoming signal (Signal 0)
   - Create signal for out_v (Signal 1, Electrical)
   - Recursively visit sub1's inline blueprint

3. Inside sub1's blueprint
   - Visit node r1, emit Component{path=/sub1/r1, type=Resistor}
     - Bind port in -> Signal 0 (via interface mapping)
     - Create signal for r1:out (Signal 2)
   - Visit node c1, emit Component{path=/sub1/c1, type=Capacitor}
     - Bind port in -> Signal 2
     - Bind port out -> Signal 1 (via interface mapping)

4. Back to root
   - Visit node l1, emit Component{path=/l1, type=LED}
     - Bind port in -> Signal 1

Output Netlist:
  Components:
    /b1        Battery     {v_out: Signal 0}
    /sub1/r1   Resistor    {in: Signal 0, out: Signal 2}
    /sub1/c1   Capacitor   {in: Signal 2, out: Signal 1}
    /l1        LED         {in: Signal 1}

  Signals:
    Signal 0: [/b1:v_out, /sub1/r1:in]        (Electrical)
    Signal 1: [/sub1/c1:out, /l1:in]          (Electrical)
    Signal 2: [/sub1/r1:out, /sub1/c1:in]     (Electrical)
```

### 4.3 Flattener Implementation

```cpp
FlatNetlist Flattener::flatten(Blueprint const& root) {
    FlatNetlist out;
    std::unordered_map<Path, SignalIndex> boundary_signals;

    // First pass: collect all interface ports and create signals
    for (auto const& wire : root.wires()) {
        process_wire(wire, Path::root(), boundary_signals, out);
    }

    // Second pass: visit all nodes and nested instances
    visit_blueprint(root, Path::root(), boundary_signals, out);

    // Build reverse index (port -> signal)
    for (auto const& conn : out.connections) {
        for (auto const& port : conn.ports) {
            out.port_to_signal[port] = conn.signal;
        }
    }

    return out;
}

void Flattener::visit_blueprint(
    Blueprint const& bp,
    Path prefix,
    std::unordered_map<Path, SignalIndex>& boundary_signals,
    FlatNetlist& out
) {
    // Visit all nodes at this level
    for (auto const& node : bp.nodes()) {
        Path node_path = arena_.make_node(prefix, node.id);

        FlatNetlist::Component comp;
        comp.path = node_path;
        comp.type = node.type;
        comp.params = node.params;

        for (auto const& port : node.iface) {
            Path port_path = arena_.make_port(node_path, port.name);
            SignalIndex sig = get_or_create_signal(port_path, port.domain,
                                                    boundary_signals, out);
            comp.port_signals.push_back({port.name, sig});
        }

        out.components.push_back(std::move(comp));
    }

    // Visit all nested instances
    for (auto const& nested : bp.nested()) {
        Path nested_path = arena_.make_nested(prefix, nested.id);

        Blueprint const* bp_to_visit = nullptr;
        if (nested.embedded && nested.inline_def) {
            bp_to_visit = &*nested.inline_def;
        } else {
            auto entry = registry_.find(nested.blueprint_id);
            if (!entry) throw std::runtime_error("Unknown blueprint: " +
                                                  std::string(nested.blueprint_id.view()));
            bp_to_visit = entry->blueprint;
        }

        // Build signal mapping for interface ports
        std::unordered_map<Path, SignalIndex> nested_boundary;
        for (auto const& iface_port : nested.iface) {
            Path iface_path = arena_.make_port(nested_path, iface_port.name);

            auto it = boundary_signals.find(iface_path);
            if (it != boundary_signals.end()) {
                nested_boundary[arena_.make_port(arena_.root(), iface_port.name)]
                    = it->second;
            }
        }

        // Recurse
        visit_blueprint(*bp_to_visit, nested_path, nested_boundary, out);
    }
}
```

---

## Part V: Serialization

### 5.1 JSON Schema

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://example.com/schemas/blueprint.json",
  "title": "Blueprint",
  "type": "object",
  "required": ["id", "version"],
  "properties": {
    "id": {
      "type": "string",
      "pattern": "^[a-zA-Z_][a-zA-Z0-9_]*$",
      "description": "Unique blueprint identifier"
    },
    "version": {
      "type": "string",
      "const": "2.0",
      "description": "Schema version for future compatibility"
    },
    "display_name": {
      "type": "string",
      "description": "Human-readable name"
    },
    "interface": {
      "type": "array",
      "items": { "$ref": "#/$defs/port" },
      "description": "Ports exposed to parent blueprints"
    },
    "nodes": {
      "type": "array",
      "items": { "$ref": "#/$defs/node" }
    },
    "wires": {
      "type": "array",
      "items": { "$ref": "#/$defs/wire" }
    },
    "nested": {
      "type": "array",
      "items": { "$ref": "#/$defs/nested" }
    }
  },
  "$defs": {
    "port": {
      "type": "object",
      "required": ["name", "domain", "direction"],
      "properties": {
        "name": { "type": "string" },
        "domain": {
          "type": "string",
          "enum": ["Electrical", "Mechanical", "Hydraulic", "Thermal", "Logical"]
        },
        "direction": {
          "type": "string",
          "enum": ["Input", "Output", "InOut"]
        }
      }
    },
    "node": {
      "type": "object",
      "required": ["id", "type"],
      "properties": {
        "id": { "type": "string" },
        "type": { "type": "string" },
        "params": {
          "type": "object",
          "additionalProperties": { "type": "number" }
        },
        "position": {
          "type": "object",
          "properties": {
            "x": { "type": "number" },
            "y": { "type": "number" }
          }
        }
      }
    },
    "wire": {
      "type": "object",
      "required": ["id", "source", "target"],
      "properties": {
        "id": { "type": "string" },
        "source": { "type": "string", "description": "Path to source port" },
        "target": { "type": "string", "description": "Path to target port" }
      }
    },
    "nested": {
      "type": "object",
      "required": ["id", "blueprint"],
      "properties": {
        "id": { "type": "string" },
        "blueprint": { "type": "string", "description": "Blueprint ID or empty if embedded" },
        "embedded": { "type": "boolean" },
        "definition": {
          "$ref": "#",
          "description": "Inline blueprint definition (only if embedded=true)"
        },
        "position": {
          "type": "object",
          "properties": {
            "x": { "type": "number" },
            "y": { "type": "number" }
          }
        }
      }
    }
  }
}
```

### 5.2 Codec Implementation

```cpp
class BlueprintCodec {
public:
    /// Serialize Blueprint -> JSON string
    static std::string encode(Blueprint const& bp);

    /// Deserialize JSON string -> Blueprint
    static std::expected<Blueprint, DecodeError> decode(
        std::string_view json,
        TypeRegistry const& registry
    );
};
```

### 5.3 Round-Trip Invariant

```cpp
/// Property-based test for serialization correctness
void test_roundtrip(Blueprint const& original) {
    // Encode
    std::string json = BlueprintCodec::encode(original);

    // Decode
    TypeRegistry registry = make_test_registry();
    auto decoded = BlueprintCodec::decode(json, registry);
    assert(decoded.has_value());

    // Re-encode
    std::string json2 = BlueprintCodec::encode(*decoded);

    // Invariant: encode(decode(encode(bp))) == encode(bp)
    assert(json == json2);

    // Stronger invariant: decoded == original
    assert(*decoded == original);
}
```

---

## Part VI: Editor Integration

### 6.1 Editor Model: Undo-Aware Container

```cpp
/// The editor's model. Wraps a Blueprint with undo/redo support.
class EditorModel {
    Blueprint current_;

    // Undo stack: previous states
    std::vector<Blueprint> undo_stack_;

    // Redo stack: states to re-apply
    std::vector<Blueprint> redo_stack_;

    size_t max_history_ = 100;

    // Derived indices (recomputed on change)
    mutable struct Indices {
        std::unordered_map<Rect, std::vector<InternedId>> spatial_index;
        std::unordered_set<std::pair<Path, Path>, PairHash> wire_set;
        bool valid = false;
    } indices_;

    void invalidate_indices() { indices_.valid = false; }
    void ensure_indices() const;

public:
    EditorModel() = default;
    explicit EditorModel(Blueprint initial) : current_(std::move(initial)) {}

    // === Current State ===
    Blueprint const& current() const { return current_; }

    // === Commands (return true if changed) ===

    bool add_node(Blueprint::Node node);
    bool remove_node(InternedId id);
    bool update_node(InternedId id, std::function<void(Blueprint::Node&)> f);

    bool add_wire(Blueprint::Wire wire);
    bool remove_wire(InternedId id);

    bool add_nested(Blueprint::Nested nested);
    bool remove_nested(InternedId id);
    bool bake_nested(InternedId id);      // Reference -> Embedded
    bool unbake_nested(InternedId id);    // Embedded -> Reference

    // === History ===

    bool can_undo() const { return !undo_stack_.empty(); }
    bool can_redo() const { return !redo_stack_.empty(); }
    void undo();
    void redo();
    void push_checkpoint();

    // === Queries (use derived indices) ===

    std::vector<InternedId> nodes_in_rect(Rect r) const;
    bool wire_exists(Path source, Path target) const;

    // === Persistence ===

    void save(std::filesystem::path path) const;
    void load(std::filesystem::path path, TypeRegistry const& registry);
};
```

### 6.2 Command Pattern Implementation

```cpp
bool EditorModel::add_node(Blueprint::Node node) {
    if (current_.find_node(node.id)) {
        return false;  // Duplicate ID
    }

    push_checkpoint();
    current_ = current_.with_node(std::move(node));
    invalidate_indices();
    return true;
}

void EditorModel::undo() {
    if (!can_undo()) return;
    redo_stack_.push_back(current_);
    current_ = undo_stack_.back();
    undo_stack_.pop_back();
    invalidate_indices();
}

void EditorModel::redo() {
    if (!can_redo()) return;
    undo_stack_.push_back(current_);
    current_ = redo_stack_.back();
    redo_stack_.pop_back();
    invalidate_indices();
}
```

---

## Part VII: Bake-In and Un-Bake Mechanics

### 7.1 Bake-In: Reference -> Embedded

```cpp
/// Converts a referenced nested blueprint to an embedded copy.
/// The embedded copy is independent of the library.
Blueprint bake_nested(Blueprint const& bp, InternedId nested_id,
                      TypeRegistry const& registry) {
    auto const* nested = bp.find_nested(nested_id);
    if (!nested) throw std::runtime_error("Nested not found");
    if (nested->embedded) throw std::runtime_error("Already embedded");

    // Look up the referenced blueprint
    Blueprint const& source = registry.at(nested->blueprint_id).blueprint;

    // Clone with a unique ID
    Blueprint copy = source.clone(SymbolTable::intern(
        std::string(bp.id().view()) + "_" + std::string(nested_id.view())
    ));

    // Create new nested with embedded copy
    Blueprint::Nested new_nested;
    new_nested.id = nested->id;
    new_nested.blueprint_id = {};   // Empty, not referenced
    new_nested.embedded = true;
    new_nested.inline_def = std::move(copy);
    new_nested.iface = nested->iface;  // Interface unchanged
    new_nested.x = nested->x;
    new_nested.y = nested->y;

    return bp.without_nested(nested_id).with_nested(std::move(new_nested));
}
```

### 7.2 Un-Bake: Embedded -> Reference

```cpp
/// Attempts to convert an embedded nested back to a reference.
/// Succeeds only if the embedded copy exactly matches a library blueprint.
struct UnbakeResult {
    Blueprint blueprint;
    InternedId referenced_id;
};

std::optional<UnbakeResult> try_unbake(
    Blueprint const& bp,
    InternedId nested_id,
    TypeRegistry const& registry
) {
    auto const* nested = bp.find_nested(nested_id);
    if (!nested || !nested->embedded || !nested->inline_def) {
        return std::nullopt;
    }

    // Search for matching blueprint in registry
    for (auto const& [id, entry] : registry) {
        if (entry.is_blueprint && entry.blueprint == *nested->inline_def) {
            Blueprint::Nested new_nested;
            new_nested.id = nested->id;
            new_nested.blueprint_id = id;
            new_nested.embedded = false;
            new_nested.inline_def = std::nullopt;
            new_nested.iface = nested->iface;
            new_nested.x = nested->x;
            new_nested.y = nested->y;

            return UnbakeResult{
                bp.without_nested(nested_id).with_nested(std::move(new_nested)),
                id
            };
        }
    }

    return std::nullopt;  // No match found
}
```

### 7.3 Recursive Bake-In (Multi-Level)

```cpp
/// Recursively bakes all nested blueprints at all levels.
Blueprint bake_all(Blueprint const& bp, TypeRegistry const& registry) {
    Blueprint result = bp;

    for (auto const& nested : bp.nested()) {
        if (nested.embedded && nested.inline_def) {
            // Recursively bake the nested blueprint's children
            Blueprint baked_child = bake_all(*nested.inline_def, registry);
            result = result.without_nested(nested.id).with_nested(
                Blueprint::Nested{
                    .id = nested.id,
                    .embedded = true,
                    .inline_def = baked_child,
                    .iface = nested.iface,
                    .x = nested.x, .y = nested.y
                }
            );
        } else if (!nested.embedded) {
            // Bake this level first, then recurse
            result = bake_nested(result, nested.id, registry);
        }
    }

    return result;
}
```

---

## Part VIII: Port Contract System

### 8.1 Interface Declaration

```json
{
  "id": "voltage_divider",
  "interface": [
    {"name": "v_in", "domain": "Electrical", "direction": "Input"},
    {"name": "v_out", "domain": "Electrical", "direction": "Output"},
    {"name": "ground", "domain": "Electrical", "direction": "InOut"}
  ]
}
```

Interface ports are bound to internal nodes via wires. One endpoint is the interface port (`/:v_in`), the other is an internal node port (`/resistor1:in`).

### 8.2 Wire Validation

```cpp
class WireValidator {
public:
    struct Result {
        bool valid;
        std::string error;
        Domain resolved_domain;
    };

    static Result validate(
        Blueprint::Wire const& wire,
        Blueprint const& bp,
        TypeRegistry const& registry
    ) {
        // 1. Resolve source and target ports
        // 2. Check domain compatibility
        // 3. Check direction compatibility (Output->Input OK, Input->Input not OK)
        // 4. Check boundary crossing (at most one level)
        // 5. Check no self-loops
    }
};
```

---

## Part IX: Invariants and Enforcement

```
+------------------------------------------------------------------------+
|                         BLUEPRINT INVARIANTS                            |
+------------------------------------------------------------------------+
|                                                                         |
|  I1. UNIQUENESS                                                         |
|      - All node IDs are unique within a blueprint                       |
|      - All wire IDs are unique within a blueprint                       |
|      - All nested IDs are unique within a blueprint                     |
|                                                                         |
|  I2. REFERENTIAL INTEGRITY                                              |
|      - Every node.type exists in the TypeRegistry                       |
|      - Every nested.blueprint_id exists (if not embedded)               |
|      - Every wire endpoint resolves to a valid port                     |
|                                                                         |
|  I3. INTERFACE COMPLETENESS                                             |
|      - Every interface port is connected to an internal node            |
|      - Interface port domains match their internal connection           |
|                                                                         |
|  I4. WIRE VALIDITY                                                      |
|      - Source and target domains match                                  |
|      - Direction compatibility is satisfied                             |
|      - At most one hierarchy boundary is crossed                        |
|      - No self-loops                                                    |
|                                                                         |
|  I5. EMBEDDED CONSISTENCY                                               |
|      - If nested.embedded == true, nested.inline_def is populated       |
|      - If nested.embedded == false, nested.blueprint_id is valid        |
|      - Embedded blueprints are fully self-contained                     |
|                                                                         |
|  I6. DETERMINISTIC ORDERING                                             |
|      - nodes_, wires_, nested_ are sorted by ID                        |
|      - Serialization is deterministic (same input -> same output)       |
|                                                                         |
+------------------------------------------------------------------------+
```

### Compile-Time Enforcement

```cpp
// Instead of bool + optional (easy to get wrong):
struct Nested_Bad {
    bool embedded;
    std::string blueprint_id;       // Required if !embedded
    std::optional<Blueprint> def;   // Required if embedded
};

// Use tagged construction (impossible to create invalid state):
class Nested {
public:
    static Nested reference(InternedId blueprint_id);
    static Nested embedded(Blueprint definition);
    // ... accessors that only return valid data
};
```

---

## Part X: Migration Strategy

### Bridge Layer (temporary)

```cpp
class BlueprintBridge {
public:
    /// Convert old FlatBlueprint to new Blueprint
    static Blueprint from_flat(FlatBlueprint const& flat,
                               TypeRegistry const& registry);

    /// Convert new Blueprint to old FlatBlueprint (for gradual rollout)
    static FlatBlueprint to_flat(Blueprint const& bp);

    /// Convert old TypeDefinition to new Blueprint
    static Blueprint from_type_def(TypeDefinition const& td,
                                   TypeRegistry const& registry);

    /// Convert new Blueprint to old TypeDefinition (for solver compatibility)
    static TypeDefinition to_type_def(Blueprint const& bp);
};
```

### Incremental Migration Timeline

```
Week 1-2: Core Types
  - InternedId, Path, PathArena
  - Interface, PortDescriptor
  - TypeRegistry (injectable)

Week 3-4: Blueprint
  - Blueprint class with nodes/wires/nested
  - Immutable mutation methods
  - Equality and hashing
  - Unit tests for all operations

Week 5-6: Serialization
  - BlueprintCodec (encode/decode)
  - JSON schema documentation
  - Round-trip tests
  - Bridge from old format

Week 7-8: Flattening
  - Flattener class
  - Path resolution
  - Signal assignment algorithm
  - Integration with solver

Week 9-10: Editor Integration
  - EditorModel with undo/redo
  - Derived indices
  - Bake/unbake operations
  - UI updates

Week 11-12: Validation & Cleanup
  - InvariantChecker
  - Remove old types
  - Remove bridges
  - Performance testing
```

---

## Part XI: Complete Example Walkthrough

### Two-Level Power Distribution

**Library blueprint** (`power_system.blueprint`):
```json
{
  "id": "power_system",
  "version": "2.0",
  "display_name": "Power Distribution System",
  "interface": [
    {"name": "main_power", "domain": "Electrical", "direction": "Input"},
    {"name": "ground", "domain": "Electrical", "direction": "InOut"},
    {"name": "bus_28v", "domain": "Electrical", "direction": "Output"}
  ],
  "nodes": [
    {"id": "main_breaker", "type": "CircuitBreaker", "params": {"rating": 50.0}},
    {"id": "transformer", "type": "Transformer", "params": {"ratio": 0.7}}
  ],
  "wires": [
    {"id": "w1", "source": "/:main_power", "target": "/main_breaker:in"},
    {"id": "w2", "source": "/main_breaker:out", "target": "/transformer:primary"},
    {"id": "w3", "source": "/transformer:secondary", "target": "/:bus_28v"},
    {"id": "w4", "source": "/:ground", "target": "/transformer:ground"}
  ]
}
```

**Top-level blueprint** (`aircraft_power.blueprint`):
```json
{
  "id": "aircraft_power",
  "version": "2.0",
  "display_name": "Aircraft Electrical System",
  "nodes": [
    {"id": "battery", "type": "Battery", "params": {"v_nominal": 28.0, "capacity": 24.0}},
    {"id": "gen_left", "type": "Generator", "params": {"v_output": 28.0, "max_current": 100.0}},
    {"id": "nav_light", "type": "LED", "params": {"color": "red"}}
  ],
  "nested": [
    {"id": "power_dist", "blueprint": "power_system", "embedded": false}
  ],
  "wires": [
    {"id": "w_batt", "source": "/battery:v_out", "target": "/power_dist:main_power"},
    {"id": "w_gen", "source": "/gen_left:v_out", "target": "/power_dist:main_power"},
    {"id": "w_gnd1", "source": "/battery:ground", "target": "/power_dist:ground"},
    {"id": "w_gnd2", "source": "/gen_left:ground", "target": "/power_dist:ground"},
    {"id": "w_nav", "source": "/power_dist:bus_28v", "target": "/nav_light:v_in"},
    {"id": "w_nav_gnd", "source": "/nav_light:ground", "target": "/power_dist:ground"}
  ]
}
```

**Flattened output:**

```
Components:
  /battery                       Battery    {v_out: S0, ground: S1}
  /gen_left                      Generator  {v_out: S0, ground: S1}
  /power_dist/main_breaker       CircuitBreaker {in: S0, out: S2}
  /power_dist/transformer        Transformer {primary: S2, secondary: S3, ground: S1}
  /nav_light                     LED        {v_in: S3, ground: S1}

Signals:
  S0 (Electrical): [/battery:v_out, /gen_left:v_out, /power_dist/main_breaker:in]
  S1 (Electrical): [/battery:ground, /gen_left:ground, /power_dist/transformer:ground, /nav_light:ground]
  S2 (Electrical): [/power_dist/main_breaker:out, /power_dist/transformer:primary]
  S3 (Electrical): [/power_dist/transformer:secondary, /nav_light:v_in]
```

---

## Part XII: Summary

### The Elegance Test

```
[x] Can you explain it in one sentence?
    "A Blueprint is an immutable graph of nodes and nested blueprints,
     flattened by recursive expansion into a netlist of connected components."

[x] Can you draw it on a napkin?
    [Blueprint] --flatten--> [FlatNetlist]
         |
         +-- Node (leaf component)
         +-- Wire (connection)
         +-- Nested --> [Blueprint] (recursion)
                   |
                   +-- Interface (ports exposed to parent)

[x] Are there fewer than 10 core types?
    InternedId, Path, PortDescriptor, Interface, Blueprint,
    TypeRegistry, FlatNetlist, EditorModel
    (8 types)

[x] Can each type be tested in isolation?
    Yes - all dependencies are injectable.

[x] Is there only one way to do each thing?
    - One representation: Blueprint
    - One flattening algorithm: Flattener::flatten()
    - One serialization format: BlueprintCodec
    - One undo mechanism: EditorModel

[x] Does invalid state require effort to create?
    Yes - types enforce invariants at construction.
```

### Key Innovations vs Current System

| Aspect | Current | New |
|--------|---------|-----|
| Representations | 3 (`Blueprint`, `FlatBlueprint`, `TypeDefinition`) | 1 (`Blueprint`) |
| Hierarchical IDs | Raw `"parent:child"` string concatenation | Typed `Path` (8 bytes, O(1) compare) |
| Serialization paths | 2 divergent (`to_flat`, `to_simulator_json`) | 1 (`BlueprintCodec`) |
| Flattening | Scattered across multiple functions | Pure `Flattener::flatten()` |
| Bake-in | Complex override application, no reverse | Copy semantics, reversible |
| Registry | Function-local static | Injectable, testable |
| Indices | 5 manually maintained | Lazy, derived on demand |
| Undo/Redo | External, bolted on | Native (immutable values) |
| Node deser | Duplicated in 2 places | Single codec |
| Wire renaming | Ad-hoc prefix stripping | Typed paths, no renaming needed |
