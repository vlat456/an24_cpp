# JSON Format v2 Design

**Date**: 2026-03-11  
**Status**: Draft for Review  
**Breaking**: Yes — no backward compatibility, all legacy removed

---

## Core Principles

1. **Everything is a Blueprint** — components, systems, the root document — all share the same schema
2. **Single Source of Truth** — no redundant data, no denormalization
3. **No Backward Compat** — clean break, delete all legacy
4. **JIT + AOT Compatible** — same schema works for runtime and code generation
5. **Explicit Hierarchy** — structure defines scope, not string prefixes

---

## v2 Schema

### Blueprint (Root Document)

```json
{
  "$schema": "https://an24.dev/schemas/blueprint-v2.json",
  "version": 2,
  
  "meta": {
    "name": "an24_main",
    "description": "AN-24 main electrical system",
    "domains": ["Electrical", "Mechanical", "Thermal"]
  },
  
  "exposes": {
    "vin": {"direction": "In", "type": "V"},
    "vout": {"direction": "Out", "type": "V"}
  },
  
  "viewport": {
    "pan": [70.7, -185.6],
    "zoom": 0.85,
    "grid": 16
  },
  
  "nodes": {
    "bat_main_1": {
      "type": "Battery",
      "pos": [96, 112],
      "size": [128, 80],
      "params": {"v_nominal": "28.0", "capacity": "1000.0"},
      "content": {"kind": "gauge", "label": "V", "unit": "V", "min": 0, "max": 30}
    },
    "main_bus": {
      "type": "Bus",
      "pos": [320, -80],
      "size": [112, 32]
    }
  },
  
  "wires": [
    {
      "id": "w_bat_to_bus",
      "from": ["bat_main_1", "v_out"],
      "to": ["main_bus", "v"],
      "routing": [[416, 144]]
    }
  ],
  
  "sub_blueprints": {
    "lamp_1": {
      "template": "library/systems/lamp_pass_through",
      "pos": [400, 300],
      "size": [200, 150],
      "collapsed": true,
      "overrides": {
        "params": {"lamp.color": "green"},
        "layout": {"vin": [50, 100], "lamp": [250, 100]},
        "routing": {"vin→lamp": [[100, 110]]}
      }
    }
  }
}
```

### Component Library Entry (Same Schema!)

```json
{
  "$schema": "https://an24.dev/schemas/blueprint-v2.json",
  "version": 2,
  
  "meta": {
    "name": "lamp_pass_through",
    "description": "Voltage pass-through with indicator lamp",
    "domains": ["Electrical"],
    "cpp_class": false
  },
  
  "exposes": {
    "vin": {"direction": "In", "type": "V"},
    "vout": {"direction": "Out", "type": "V"}
  },
  
  "nodes": {
    "vin": {"type": "BlueprintInput", "params": {"exposed_type": "V"}},
    "lamp": {"type": "IndicatorLight", "params": {"color": "red"}},
    "vout": {"type": "BlueprintOutput", "params": {"exposed_type": "V"}}
  },
  
  "wires": [
    {"id": "w_in", "from": ["vin", "port"], "to": ["lamp", "v_in"]},
    {"id": "w_out", "from": ["lamp", "v_out"], "to": ["vout", "port"]}
  ]
}
```

### C++ Component (Native, Not Blueprint)

```json
{
  "$schema": "https://an24.dev/schemas/blueprint-v2.json",
  "version": 2,
  
  "meta": {
    "name": "RU19A",
    "description": "RU19A-300 Auxiliary Power Unit",
    "domains": ["Electrical", "Mechanical", "Thermal"],
    "cpp_class": true
  },
  
  "exposes": {
    "v_start": {"direction": "In", "type": "V"},
    "v_bus": {"direction": "Out", "type": "V"},
    "k_mod": {"direction": "In", "type": "V"},
    "rpm_out": {"direction": "Out", "type": "RPM"},
    "t4_out": {"direction": "Out", "type": "Temperature"}
  },
  
  "params": {
    "target_rpm": {"type": "float", "default": "16000.0"}
  }
}
```

---

## Key Design Decisions

### 1. `nodes` as Object, Not Array

**Before:**
```json
"devices": [
  {"name": "bat_1", "classname": "Battery", ...},
  {"name": "bus_1", "classname": "Bus", ...}
]
```

**After:**
```json
"nodes": {
  "bat_1": {"type": "Battery", ...},
  "bus_1": {"type": "Bus", ...}
}
```

**Benefits:**
- O(1) lookup by ID
- No duplicate IDs possible (object keys are unique)
- No `name` field needed — key IS the id
- Self-documenting: `nodes["bat_1"]` vs `devices.find(d => d.name == "bat_1")`

### 2. `exposes` Instead of Special Nodes

**Before:**
```json
"nodes": [
  {"name": "vin", "classname": "BlueprintInput", "params": {"exposed_type": "V"}},
  {"name": "vout", "classname": "BlueprintOutput", "params": {"exposed_type": "V"}}
]
```

**After:**
```json
"exposes": {
  "vin": {"direction": "In", "type": "V"},
  "vout": {"direction": "Out", "type": "V"}
}
```

**Benefits:**
- BlueprintInput/BlueprintOutput are implementation details, not user-facing
- Cleaner library definitions
- AOT can generate proper port structs directly
- Internal nodes are purely internal

### 3. Wire Endpoints as Arrays

**Before:**
```json
{"from": "bat_main_1.v_out", "to": "main_bus.v"}
```

**After:**
```json
{"from": ["bat_main_1", "v_out"], "to": ["main_bus", "v"]}
```

**Benefits:**
- No string parsing required
- Type-safe in strongly-typed languages
- Clearer structure: `[node, port]`

### 4. Unprefixed Keys in Overrides

**Before:**
```json
"layout_override": {
  "lamp_1:vin": {"x": 50, "y": 100},
  "lamp_1:lamp": {"x": 250, "y": 100}
}
```

**After:**
```json
"overrides": {
  "layout": {
    "vin": [50, 100],
    "lamp": [250, 100]
  }
}
```

**Benefits:**
- Scope is structural (inside `sub_blueprints.lamp_1`)
- No `sbi.id + ":" + local_id` concatenations
- Keys match the template's internal node IDs

### 5. Positions as Arrays

**Before:**
```json
"pos": {"x": 96, "y": 112}
```

**After:**
```json
"pos": [96, 112]
```

**Benefits:**
- 50% smaller JSON
- Natural for `[x, y]` coordinate pairs
- Faster parsing (no string key lookups)

### 6. No `baked_in` Flag — Just Embedded Nodes

**Before:** `baked_in` flag changes serialization behavior entirely.

**After:** Two clear modes:

**Reference mode** (template + overrides):
```json
"sub_blueprints": {
  "lamp_1": {
    "template": "library/systems/lamp_pass_through",
    "overrides": {...}
  }
}
```

**Embedded mode** (nodes inline, template kept for provenance):
```json
"sub_blueprints": {
  "lamp_1": {
    "template": "library/systems/lamp_pass_through",
    "pos": [400, 300],
    "size": [200, 150],
    "collapsed": false,
    "nodes": {
      "vin": {"type": "BlueprintInput", "pos": [50, 100], "params": {"exposed_type": "V"}},
      "lamp": {"type": "IndicatorLight", "pos": [250, 100], "params": {"color": "green"}},
      "vout": {"type": "BlueprintOutput", "pos": [450, 100], "params": {"exposed_type": "V"}}
    },
    "wires": [
      {"id": "w_in", "from": ["vin", "port"], "to": ["lamp", "v_in"]},
      {"id": "w_out", "from": ["lamp", "v_out"], "to": ["vout", "port"]}
    ]
  }
}
```

No `baked_in` flag — presence of `nodes` determines embedded mode. `template` kept for provenance ("Edit Original" feature).

---

## C++ Data Structures

```cpp
// v2 structures — clean, no legacy

struct BlueprintV2 {
    std::string name;
    std::string description;
    std::vector<std::string> domains;
    bool cpp_class = false;
    
    std::map<std::string, ExposedPort> exposes;
    std::map<std::string, ParamDef> params;  // only for cpp_class=true
    
    std::map<std::string, NodeV2> nodes;
    std::vector<WireV2> wires;
    std::map<std::string, SubBlueprintV2> sub_blueprints;
    
    // Editor-only (not in library definitions)
    Viewport viewport;
};

struct NodeV2 {
    std::string type;  // component type
    Pt pos;
    Pt size;
    std::map<std::string, std::string> params;
    std::optional<ContentV2> content;
    std::optional<NodeColor> color;
};

struct WireV2 {
    std::string id;
    WireEnd from;  // {node_id, port_name}
    WireEnd to;
    std::vector<Pt> routing;
};

struct SubBlueprintV2 {
    // Reference mode
    std::optional<std::string> template_path;
    
    // Embedded mode (mutually exclusive with template)
    std::map<std::string, NodeV2> nodes;
    std::vector<WireV2> wires;
    
    // Common
    Pt pos;
    Pt size;
    bool collapsed = false;
    
    // Overrides (reference mode only)
    std::map<std::string, std::string> params_override;
    std::map<std::string, Pt> layout_override;
    std::map<std::string, std::vector<Pt>> routing_override;
};

struct ExposedPort {
    PortDirection direction;
    PortType type;
};
```

---

## Migration Plan

### Phase 1: New Parser (1-2 days)

```cpp
// New file: src/editor/visual/scene/persist_v2.cpp
std::optional<BlueprintV2> parse_blueprint_v2(const std::string& json);
std::string serialize_blueprint_v2(const BlueprintV2& bp);
```

### Phase 2: Convert Library (1 day)

```bash
# One-time script
python scripts/convert_v1_to_v2.py library/ library_v2/
```

### Phase 3: Delete Old Code (1 day)

Files to delete/rewrite:
- `src/editor/visual/scene/persist.cpp` → rewrite as v2 only
- Remove all `baked_in` logic
- Remove `collapsed_groups` handling
- Remove dedup code (no longer needed)

### Phase 4: Update Tests (1-2 days)

- Convert all test JSON to v2
- Update test fixtures
- Remove backward-compat tests

---

## What Gets Deleted

| Code | Reason |
|------|--------|
| `baked_in` flag | Replaced by `template` vs `nodes` presence |
| `internal_node_ids` | Computed from template |
| `collapsed_groups` | Redundant with `sub_blueprints` |
| All dedup code | Object keys are unique |
| `group_id` on nodes | Structure defines hierarchy |
| `name`/`classname`/`display_name` | Replaced by key + `type` + `label` |
| `connections` array | Unified with `wires` |
| String-parsing wire endpoints | Arrays `[node, port]` |
| Prefix concatenation | Scope is structural |

---

## Everything is a Blueprint

This design enforces the principle uniformly:

| Entity | Schema | Notes |
|--------|--------|-------|
| Root document | Blueprint | Has `viewport`, `sub_blueprints` |
| Library component | Blueprint | Has `cpp_class: true`, no `viewport` |
| Library system | Blueprint | Has `exposes`, `nodes`, `wires` |
| Sub-blueprint instance | Blueprint subset | Either `template` ref or embedded `nodes` |

**No special cases.** The parser, the code generator, and the runtime all see the same structure.

---

## Design Decisions (Resolved)

| Question | Decision | Rationale |
|----------|----------|-----------|
| `exposes` vs `ports` | **`exposes`** | Semantically precise: what the blueprint exposes externally |
| Wire IDs | **Keep** | Needed for routing points binding, may be useful for undo/redo, diffs |
| Template path | **Full path** `library/systems/lamp_pass_through` | Explicit, can reference outside library |
| Provenance in embedded | **Keep `template` field** | Useful for "Edit Original", debugging |
| Viewport scope | **Root only** | Library = components, not editable documents |

---

## File Extension

Use `.blueprint` extension for all blueprint files:

```
library/
├── Battery.blueprint          # C++ component
├── RU19A.blueprint            # C++ component
├── systems/
│   ├── lamp_pass_through.blueprint
│   ├── simple_battery.blueprint
│   └── RU19A.blueprint
└── blueprints/
    └── an24_main.blueprint    # Root document
```

Benefits:
- Clear file type identification
- Editor/IDE syntax highlighting association
- Distinct from generic JSON config files
- Convention over configuration

---

## Next Steps

1. Review and approve this design
2. Implement v2 parser/serializer
3. Convert library files
4. Delete v1 code
5. Update all tests
