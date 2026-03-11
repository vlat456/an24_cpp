# Sub-Blueprint References — Implementation Guide

> For AI coding agent. TDD approach: **write failing tests first**, then implement.
> Companion to `docs/sub_blueprint_references.md` (architecture).

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Current State Inventory](#2-current-state-inventory)
3. [Phase 1: Data Structures + Tests](#3-phase-1-data-structures--tests)
4. [Phase 2: Recursive Loading + Tests](#4-phase-2-recursive-loading--tests)
5. [Phase 3: Persistence + Tests](#5-phase-3-persistence--tests)
6. [Phase 4: Bake-In + Tests](#6-phase-4-bake-in--tests)
7. [Phase 5: Editor Integration + Tests](#7-phase-5-editor-integration--tests)
8. [Phase 6: Cleanup](#8-phase-6-cleanup)
9. [Phase 7: Hierarchical AOT Codegen + Tests](#9-phase-7-hierarchical-aot-codegen--tests)
10. [Verification Checklist](#10-verification-checklist)

---

## 1. Architecture Overview

### What Changes

```
BEFORE:
  TypeRegistry  ──has──>  TypeDefinition (types map)
  Blueprint     ──has──>  CollapsedGroup (inline expanded devices)
  addBlueprint()          expands + prefixes + stores inline

AFTER:
  TypeRegistry  ──has──>  TypeDefinition (types map, now with sub_blueprints field)
  Blueprint     ──has──>  SubBlueprintInstance (unified: baked_in flag controls mode)
  addBlueprint()          creates SubBlueprintInstance (baked_in=false)
  expandOnLoad()          resolves references → flat devices at load time
  bakeIn()                sets baked_in=true, flattens overrides (one-way)
```

### What Does NOT Change

- `struct Blueprint` in `src/editor/data/blueprint.h` — keeps nodes, wires, pan, zoom
- `struct Node` in `src/editor/data/node.h` — keeps all fields as-is
- `struct Wire` in `src/editor/data/wire.h` — unchanged
- Simulator path (ParserContext, Connection) — unchanged
- Existing primitive AOT codegen — unchanged (composites are additive)
- `expand_type_definition()` — unchanged (still used for bake-in and loading)

### Naming Decisions

| Concept           | Struct name            | Rationale                                        |
| ----------------- | ---------------------- | ------------------------------------------------ |
| Registry entry    | `TypeDefinition`       | Keep existing name, extend with `sub_blueprints` |
| Editor document   | `Blueprint`            | Unchanged                                        |
| Sub-bp (any mode) | `SubBlueprintInstance` | New struct, `baked_in` flag controls behavior    |

> **Why not rename TypeDefinition → TypeBlueprint?** Minimizes diff, avoids touching
> every file that uses `TypeDefinition`. The `sub_blueprints` field is additive.

---

## 2. Current State Inventory

### Files to Modify

| File                                  | Purpose                                            | What Changes                                                                    |
| ------------------------------------- | -------------------------------------------------- | ------------------------------------------------------------------------------- |
| `src/json_parser/json_parser.h`       | TypeDefinition, TypeRegistry, SubBlueprintInstance | Add `sub_blueprints` to TypeDefinition, add `SubBlueprintInstance` struct       |
| `src/json_parser/json_parser.cpp`     | Parsing, registry loading                          | Parse `sub_blueprints`, recursive load, cycle detection                         |
| `src/editor/data/blueprint.h`         | Blueprint, CollapsedGroup                          | **Remove `CollapsedGroup`**, add `sub_blueprint_instances` with `baked_in` flag |
| `src/editor/data/blueprint.cpp`       | expand_type_definition                             | Add `expand_sub_blueprints()` helper                                            |
| `src/editor/visual/scene/persist.cpp` | Save/Load                                          | Save sub_blueprint_instances as references; load with expansion                 |
| `src/editor/document.cpp`             | addBlueprint, bakeInSubBlueprint                   | New `addSubBlueprintReference()`, `bakeInSubBlueprint()`                        |
| `src/editor/window_system.h`          | Context menu state                                 | Add bake-in to node context menu                                                |
| `examples/an24_editor.cpp`            | UI rendering                                       | Render "Bake In" context menu item                                              |

### New Test Files

| File                               | Tests                                                                      |
| ---------------------------------- | -------------------------------------------------------------------------- |
| `tests/test_sub_blueprint_ref.cpp` | SubBlueprintInstance CRUD, recursive load, cycle detection, override merge |
| `tests/test_bake_in.cpp`           | Bake-in conversion, params flatten, save/load roundtrip                    |

### Existing Tests (must stay green)

| File                                    | Count | What they test                                             |
| --------------------------------------- | ----- | ---------------------------------------------------------- |
| `tests/test_editor_hierarchical.cpp`    | 23    | Collapsed rendering, ports, persistence, voltage, group_id |
| `tests/test_document_window_system.cpp` | 36    | Document/WindowSystem MDI operations                       |
| All other tests                         | —     | Must not break                                             |

---

## 3. Phase 1: Data Structures + Tests

### Step 1.1: Write Failing Tests (test_sub_blueprint_ref.cpp)

Create `tests/test_sub_blueprint_ref.cpp`. Register in `tests/CMakeLists.txt`.

```cpp
#include <gtest/gtest.h>
#include "json_parser/json_parser.h"
#include "editor/data/blueprint.h"

using namespace an24;

// ============================================================
// SubBlueprintInstance struct
// ============================================================

TEST(SubBlueprintInstance, DefaultConstruction) {
    SubBlueprintInstance sbi;
    EXPECT_TRUE(sbi.id.empty());
    EXPECT_TRUE(sbi.blueprint_path.empty());
    EXPECT_TRUE(sbi.type_name.empty());
    EXPECT_FALSE(sbi.baked_in);
    EXPECT_EQ(sbi.pos.x, 0.0f);
    EXPECT_EQ(sbi.pos.y, 0.0f);
    EXPECT_TRUE(sbi.params_override.empty());
    EXPECT_TRUE(sbi.layout_override.empty());
    EXPECT_TRUE(sbi.internal_routing.empty());
    EXPECT_TRUE(sbi.internal_node_ids.empty());
}

TEST(SubBlueprintInstance, FullConstruction) {
    SubBlueprintInstance sbi;
    sbi.id = "lamp_1";
    sbi.blueprint_path = "library/systems/lamp_pass_through.json";
    sbi.type_name = "lamp_pass_through";
    sbi.pos = {400.0f, 300.0f};
    sbi.size = {120.0f, 80.0f};
    sbi.params_override["lamp.color"] = "green";
    sbi.layout_override["vin"] = {350.0f, 300.0f};
    sbi.internal_routing["vin.port->lamp.v_in"] = {{375.0f, 310.0f}};

    EXPECT_EQ(sbi.id, "lamp_1");
    EXPECT_EQ(sbi.blueprint_path, "library/systems/lamp_pass_through.json");
    EXPECT_EQ(sbi.params_override.size(), 1u);
    EXPECT_EQ(sbi.layout_override.size(), 1u);
    EXPECT_EQ(sbi.internal_routing.size(), 1u);
}

// ============================================================
// TypeDefinition now has sub_blueprints field
// ============================================================

TEST(TypeDefinition, HasSubBlueprintsField) {
    TypeDefinition td;
    EXPECT_TRUE(td.sub_blueprints.empty());

    SubBlueprintInstance sbi;
    sbi.id = "bat_1";
    sbi.type_name = "simple_battery";
    td.sub_blueprints.push_back(sbi);
    EXPECT_EQ(td.sub_blueprints.size(), 1u);
}

// ============================================================
// Blueprint has sub_blueprint_instances field
// ============================================================

TEST(BlueprintSubRef, HasSubBlueprintInstancesField) {
    Blueprint bp;
    EXPECT_TRUE(bp.sub_blueprint_instances.empty());

    SubBlueprintInstance sbi;
    sbi.id = "lamp_1";
    sbi.type_name = "lamp_pass_through";
    bp.sub_blueprint_instances.push_back(sbi);
    EXPECT_EQ(bp.sub_blueprint_instances.size(), 1u);
}

TEST(BlueprintSubRef, FindSubBlueprintById) {
    Blueprint bp;
    SubBlueprintInstance sbi;
    sbi.id = "lamp_1";
    sbi.type_name = "lamp_pass_through";
    bp.sub_blueprint_instances.push_back(sbi);

    // find_sub_blueprint_instance(id) → pointer or nullptr
    auto* found = bp.find_sub_blueprint_instance("lamp_1");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->type_name, "lamp_pass_through");

    EXPECT_EQ(bp.find_sub_blueprint_instance("nonexistent"), nullptr);
}

TEST(BlueprintSubRef, RemoveSubBlueprintById) {
    Blueprint bp;
    SubBlueprintInstance sbi;
    sbi.id = "lamp_1";
    bp.sub_blueprint_instances.push_back(sbi);

    bool removed = bp.remove_sub_blueprint_instance("lamp_1");
    EXPECT_TRUE(removed);
    EXPECT_TRUE(bp.sub_blueprint_instances.empty());

    EXPECT_FALSE(bp.remove_sub_blueprint_instance("nonexistent"));
}
```

**Run: expect compile failures** (SubBlueprintInstance doesn't exist yet, Blueprint has no `sub_blueprint_instances`, etc.)

### Step 1.2: Implement Data Structures

#### 1.2a: Add `SubBlueprintInstance` to `src/json_parser/json_parser.h`

Insert **before** `struct TypeDefinition`:

```cpp
/// Instance of a sub-blueprint — reference (baked_in=false) or embedded (baked_in=true).
struct SubBlueprintInstance {
    std::string id;                  // Unique instance ID: "lamp_1"
    std::string blueprint_path;      // "library/systems/lamp_pass_through.json"
    std::string type_name;           // "lamp_pass_through" (for UI display)

    bool baked_in = false;           // true = inline devices saved to JSON
                                     // false = expand from library file on load

    // Layout of collapsed node
    Pt pos;
    Pt size;

    // Instance-specific overrides (meaningful only when baked_in = false)
    std::map<std::string, std::string> params_override;
    std::map<std::string, Pt> layout_override;
    std::map<std::string, std::vector<Pt>> internal_routing;

    // Internal node tracking (always populated at runtime)
    std::vector<std::string> internal_node_ids;
};
```

> **Note:** `Pt` is from `src/editor/data/node.h`. If json_parser must not depend on editor
> types, use `std::pair<float,float>` for pos/size/layout and convert at the boundary.
> Check if `Pt` is already available — if not, use pairs and convert in persist.cpp.

#### 1.2b: Add `sub_blueprints` field to `TypeDefinition`

In `struct TypeDefinition`, add after `std::vector<Connection> connections;`:

```cpp
    // Sub-blueprint references (cpp_class = false composites only)
    std::vector<SubBlueprintInstance> sub_blueprints;
```

#### 1.2c: Replace `collapsed_groups` with `sub_blueprint_instances` in `struct Blueprint`

In `src/editor/data/blueprint.h`, **remove** the `collapsed_groups` vector and `CollapsedGroup` struct.
Replace with:

```cpp
    std::vector<SubBlueprintInstance> sub_blueprint_instances;

    SubBlueprintInstance* find_sub_blueprint_instance(const std::string& id);
    const SubBlueprintInstance* find_sub_blueprint_instance(const std::string& id) const;
    bool remove_sub_blueprint_instance(const std::string& id);
```

#### 1.2d: Implement helpers in `src/editor/data/blueprint.cpp`

```cpp
SubBlueprintInstance* Blueprint::find_sub_blueprint_instance(const std::string& id) {
    for (auto& sbi : sub_blueprint_instances)
        if (sbi.id == id) return &sbi;
    return nullptr;
}

const SubBlueprintInstance* Blueprint::find_sub_blueprint_instance(const std::string& id) const {
    for (const auto& sbi : sub_blueprint_instances)
        if (sbi.id == id) return &sbi;
    return nullptr;
}

bool Blueprint::remove_sub_blueprint_instance(const std::string& id) {
    auto it = std::find_if(sub_blueprint_instances.begin(), sub_blueprint_instances.end(),
                           [&](const SubBlueprintInstance& s) { return s.id == id; });
    if (it == sub_blueprint_instances.end()) return false;
    sub_blueprint_instances.erase(it);
    return true;
}
```

#### 1.2e: Include header fix

Add `#include <map>` to `json_parser.h` if not present. Add `#include "json_parser/json_parser.h"` to `blueprint.h` if `SubBlueprintInstance` is defined there. **Or** define `SubBlueprintInstance` in `blueprint.h` and forward-declare in `json_parser.h`. Prefer the simplest approach.

> **Dependency check:** `json_parser.h` currently does NOT depend on editor headers.
> `SubBlueprintInstance` uses `Pt` (editor type). Two options:
>
> 1. Define `SubBlueprintInstance` in `src/editor/data/blueprint.h` (editor-only)
> 2. Use `std::pair<float,float>` in json_parser, convert later
>
> **Recommended:** Option 1 — define in `blueprint.h`, since override tracking is editor concern.
> TypeDefinition in json_parser.h keeps `sub_blueprints` as a simpler struct:
>
> ```cpp
> // In json_parser.h (minimal, no Pt dependency)
> struct SubBlueprintRef {
>     std::string id;
>     std::string blueprint_path;
>     std::string type_name;
>     std::optional<std::pair<float,float>> pos;
>     std::optional<std::pair<float,float>> size;
>     std::map<std::string, std::string> params_override;
> };
> ```
>
> And `SubBlueprintInstance` in `blueprint.h` extends this with editor-specific fields
> (Pt pos, Pt size, layout_override, internal_routing).

### Step 1.3: Run Tests — All Should Pass

```bash
cmake --build build -j$(nproc) && cd build && ctest -R "sub_blueprint_ref" --output-on-failure
```

Also run existing tests:

```bash
ctest --output-on-failure
```

---

## 4. Phase 2: Recursive Loading + Tests

### Step 2.1: Write Failing Tests

Add to `tests/test_sub_blueprint_ref.cpp`:

```cpp
// ============================================================
// JSON Parsing: sub_blueprints array
// ============================================================

TEST(SubBlueprintParse, ParseSubBlueprintsArray) {
    // JSON with sub_blueprints
    std::string json_str = R"({
        "classname": "my_circuit",
        "cpp_class": false,
        "domains": ["Electrical"],
        "sub_blueprints": [
            {
                "id": "lamp_1",
                "blueprint_path": "library/systems/lamp_pass_through.json",
                "type_name": "lamp_pass_through",
                "pos": {"x": 400, "y": 300},
                "size": {"x": 120, "y": 80},
                "params_override": {
                    "lamp.color": "green"
                }
            }
        ],
        "devices": [
            {"name": "bat", "classname": "Battery"}
        ],
        "connections": [
            {"from": "bat.v_out", "to": "lamp_1.vin"}
        ]
    })";

    auto j = nlohmann::json::parse(json_str);
    auto td = an24::parse_type_definition(j);

    ASSERT_EQ(td.sub_blueprints.size(), 1u);
    EXPECT_EQ(td.sub_blueprints[0].id, "lamp_1");
    EXPECT_EQ(td.sub_blueprints[0].blueprint_path, "library/systems/lamp_pass_through.json");
    EXPECT_EQ(td.sub_blueprints[0].type_name, "lamp_pass_through");
    EXPECT_EQ(td.sub_blueprints[0].params_override.at("lamp.color"), "green");

    // Regular devices still parsed
    EXPECT_EQ(td.devices.size(), 1u);
    EXPECT_EQ(td.connections.size(), 1u);
}

TEST(SubBlueprintParse, NoSubBlueprintsField_EmptyVector) {
    std::string json_str = R"({
        "classname": "Battery",
        "cpp_class": true,
        "ports": {"v_in": {"direction": "In", "type": "V"}}
    })";

    auto j = nlohmann::json::parse(json_str);
    auto td = an24::parse_type_definition(j);
    EXPECT_TRUE(td.sub_blueprints.empty());
}

// ============================================================
// Cycle Detection
// ============================================================

TEST(CycleDetection, DirectSelfReference_Throws) {
    // Create a TypeDefinition that references itself
    an24::TypeRegistry registry;
    an24::TypeDefinition td;
    td.classname = "self_ref";
    td.cpp_class = false;
    an24::SubBlueprintRef ref;
    ref.id = "me";
    ref.blueprint_path = "self_ref";  // references own classname
    ref.type_name = "self_ref";
    td.sub_blueprints.push_back(ref);
    registry.types["self_ref"] = td;

    std::set<std::string> loading_stack;
    EXPECT_THROW(
        an24::expand_sub_blueprint_references(td, registry, loading_stack),
        std::runtime_error
    );
}

TEST(CycleDetection, IndirectCycle_Throws) {
    an24::TypeRegistry registry;

    // A references B
    an24::TypeDefinition td_a;
    td_a.classname = "cycle_a";
    td_a.cpp_class = false;
    an24::SubBlueprintRef ref_b;
    ref_b.id = "b_inst";
    ref_b.blueprint_path = "cycle_b";
    ref_b.type_name = "cycle_b";
    td_a.sub_blueprints.push_back(ref_b);
    registry.types["cycle_a"] = td_a;

    // B references A
    an24::TypeDefinition td_b;
    td_b.classname = "cycle_b";
    td_b.cpp_class = false;
    an24::SubBlueprintRef ref_a;
    ref_a.id = "a_inst";
    ref_a.blueprint_path = "cycle_a";
    ref_a.type_name = "cycle_a";
    td_b.sub_blueprints.push_back(ref_a);
    registry.types["cycle_b"] = td_b;

    std::set<std::string> loading_stack;
    EXPECT_THROW(
        an24::expand_sub_blueprint_references(td_a, registry, loading_stack),
        std::runtime_error
    );
}

// ============================================================
// Recursive Expansion
// ============================================================

TEST(SubBlueprintExpand, SingleLevel_FlattensPrefixed) {
    // Setup: lamp_pass_through has 3 internal devices
    an24::TypeRegistry registry;

    an24::TypeDefinition lamp;
    lamp.classname = "lamp_pass_through";
    lamp.cpp_class = false;
    an24::DeviceInstance d_vin;  d_vin.name = "vin";  d_vin.classname = "BlueprintInput";
    an24::DeviceInstance d_lamp; d_lamp.name = "lamp"; d_lamp.classname = "IndicatorLight";
    an24::DeviceInstance d_vout; d_vout.name = "vout"; d_vout.classname = "BlueprintOutput";
    lamp.devices = {d_vin, d_lamp, d_vout};
    lamp.connections = {{"vin.port", "lamp.v_in", {}}, {"lamp.v_out", "vout.port", {}}};
    registry.types["lamp_pass_through"] = lamp;

    // Parent references lamp
    an24::TypeDefinition parent;
    parent.classname = "my_circuit";
    parent.cpp_class = false;
    an24::DeviceInstance d_bat; d_bat.name = "bat"; d_bat.classname = "Battery";
    parent.devices = {d_bat};
    an24::SubBlueprintRef ref;
    ref.id = "lamp_1";
    ref.type_name = "lamp_pass_through";
    parent.sub_blueprints.push_back(ref);
    registry.types["my_circuit"] = parent;

    std::set<std::string> stack;
    auto result = an24::expand_sub_blueprint_references(parent, registry, stack);

    // Should have: bat (top-level) + lamp_1:vin + lamp_1:lamp + lamp_1:vout = 4 devices
    EXPECT_EQ(result.devices.size(), 4u);

    // Check prefixed names
    bool found_bat = false, found_vin = false, found_lamp = false, found_vout = false;
    for (const auto& d : result.devices) {
        if (d.name == "bat") found_bat = true;
        if (d.name == "lamp_1:vin") found_vin = true;
        if (d.name == "lamp_1:lamp") found_lamp = true;
        if (d.name == "lamp_1:vout") found_vout = true;
    }
    EXPECT_TRUE(found_bat);
    EXPECT_TRUE(found_vin);
    EXPECT_TRUE(found_lamp);
    EXPECT_TRUE(found_vout);

    // Connections also prefixed
    // parent connections + 2 internal prefixed = 2 internal
    // (parent's top-level connections stay as-is, internal get prefixed)
    bool found_internal_conn = false;
    for (const auto& c : result.connections) {
        if (c.from == "lamp_1:vin.port" && c.to == "lamp_1:lamp.v_in")
            found_internal_conn = true;
    }
    EXPECT_TRUE(found_internal_conn);
}

TEST(SubBlueprintExpand, OverrideParams_Applied) {
    an24::TypeRegistry registry;

    an24::TypeDefinition lamp;
    lamp.classname = "lamp_pass_through";
    lamp.cpp_class = false;
    an24::DeviceInstance d_lamp; d_lamp.name = "lamp"; d_lamp.classname = "IndicatorLight";
    d_lamp.params["color"] = "red";
    lamp.devices = {d_lamp};
    registry.types["lamp_pass_through"] = lamp;

    an24::TypeDefinition parent;
    parent.classname = "my_circuit";
    parent.cpp_class = false;
    an24::SubBlueprintRef ref;
    ref.id = "lamp_1";
    ref.type_name = "lamp_pass_through";
    ref.params_override["lamp.color"] = "green";  // Override color
    parent.sub_blueprints.push_back(ref);
    registry.types["my_circuit"] = parent;

    std::set<std::string> stack;
    auto result = an24::expand_sub_blueprint_references(parent, registry, stack);

    // Find the lamp device, check param was overridden
    for (const auto& d : result.devices) {
        if (d.name == "lamp_1:lamp") {
            EXPECT_EQ(d.params.at("color"), "green");
            return;
        }
    }
    FAIL() << "lamp_1:lamp device not found in expanded result";
}

TEST(SubBlueprintExpand, TwoLevelsDeep_FullyPrefixed) {
    an24::TypeRegistry registry;

    // Level 2: simple_battery has bat + vin + vout
    an24::TypeDefinition simple_bat;
    simple_bat.classname = "simple_battery";
    simple_bat.cpp_class = false;
    an24::DeviceInstance d_bat; d_bat.name = "bat"; d_bat.classname = "Battery";
    an24::DeviceInstance d_vin; d_vin.name = "vin"; d_vin.classname = "BlueprintInput";
    simple_bat.devices = {d_bat, d_vin};
    registry.types["simple_battery"] = simple_bat;

    // Level 1: battery_bank references simple_battery
    an24::TypeDefinition bank;
    bank.classname = "battery_bank";
    bank.cpp_class = false;
    an24::SubBlueprintRef ref;
    ref.id = "sb_1";
    ref.type_name = "simple_battery";
    bank.sub_blueprints.push_back(ref);
    registry.types["battery_bank"] = bank;

    // Level 0: top references battery_bank
    an24::TypeDefinition top;
    top.classname = "top";
    top.cpp_class = false;
    an24::SubBlueprintRef ref2;
    ref2.id = "bank_1";
    ref2.type_name = "battery_bank";
    top.sub_blueprints.push_back(ref2);
    registry.types["top"] = top;

    std::set<std::string> stack;
    auto result = an24::expand_sub_blueprint_references(top, registry, stack);

    // Expect: bank_1:sb_1:bat, bank_1:sb_1:vin
    bool found_deep = false;
    for (const auto& d : result.devices) {
        if (d.name == "bank_1:sb_1:bat") found_deep = true;
    }
    EXPECT_TRUE(found_deep) << "Two-level deep prefix bank_1:sb_1:bat not found";
}
```

**Run: expect compile failures** (`expand_sub_blueprint_references` doesn't exist, `SubBlueprintRef` doesn't exist)

### Step 2.2: Implement Recursive Loading

#### 2.2a: `parse_type_definition()` — parse `sub_blueprints` array

In `src/json_parser/json_parser.cpp`, inside `parse_type_definition()`, add after existing device/connection parsing:

```cpp
// Parse sub_blueprints array (references to other blueprints)
if (j.contains("sub_blueprints") && j["sub_blueprints"].is_array()) {
    for (const auto& sbj : j["sub_blueprints"]) {
        SubBlueprintRef ref;
        ref.id = sbj.value("id", "");
        ref.blueprint_path = sbj.value("blueprint_path", "");
        ref.type_name = sbj.value("type_name", "");
        if (sbj.contains("pos"))
            ref.pos = {sbj["pos"].value("x", 0.0f), sbj["pos"].value("y", 0.0f)};
        if (sbj.contains("size"))
            ref.size = {sbj["size"].value("x", 0.0f), sbj["size"].value("y", 0.0f)};
        if (sbj.contains("params_override") && sbj["params_override"].is_object()) {
            for (auto& [k, v] : sbj["params_override"].items())
                ref.params_override[k] = v.get<std::string>();
        }
        td.sub_blueprints.push_back(std::move(ref));
    }
}
```

#### 2.2b: `expand_sub_blueprint_references()` — recursive expansion with cycle detection

New function in `src/json_parser/json_parser.cpp` (declare in `.h`):

```cpp
/// Expand sub_blueprint references into flat devices + connections.
/// Throws std::runtime_error on circular references.
/// loading_stack tracks ancestors for cycle detection — pass empty set at top call.
TypeDefinition expand_sub_blueprint_references(
    const TypeDefinition& td,
    const TypeRegistry& registry,
    std::set<std::string>& loading_stack);
```

Implementation:

```cpp
TypeDefinition expand_sub_blueprint_references(
    const TypeDefinition& td,
    const TypeRegistry& registry,
    std::set<std::string>& loading_stack)
{
    if (td.cpp_class) return td;  // Primitives: nothing to expand

    // Cycle check
    if (!loading_stack.insert(td.classname).second) {
        throw std::runtime_error("Circular sub-blueprint reference: " + td.classname);
    }

    TypeDefinition result = td;  // Copy all fields
    // Remove sub_blueprints from result (they get expanded into devices/connections)
    result.sub_blueprints.clear();

    for (const auto& ref : td.sub_blueprints) {
        const auto* sub_td = registry.get(ref.type_name);
        if (!sub_td) {
            throw std::runtime_error(
                "Sub-blueprint '" + ref.type_name + "' not found in TypeRegistry"
                " (referenced by '" + td.classname + "' as '" + ref.id + "')");
        }

        // Recurse: expand the sub-blueprint's own sub_blueprints
        auto expanded = expand_sub_blueprint_references(*sub_td, registry, loading_stack);

        // Prefix internal devices
        for (auto& dev : expanded.devices) {
            dev.name = ref.id + ":" + dev.name;

            // Apply param overrides: key format is "device.param"
            for (const auto& [override_key, override_val] : ref.params_override) {
                auto dot = override_key.find('.');
                if (dot == std::string::npos) continue;
                std::string dev_name = override_key.substr(0, dot);
                std::string param_name = override_key.substr(dot + 1);
                // Match against unprefixed name (before we prefixed it)
                // The device was already prefixed above, so strip prefix to compare
                std::string unprefixed = dev.name.substr(ref.id.size() + 1);
                if (unprefixed == dev_name) {
                    dev.params[param_name] = override_val;
                }
            }

            result.devices.push_back(std::move(dev));
        }

        // Prefix internal connections
        for (auto& conn : expanded.connections) {
            conn.from = ref.id + ":" + conn.from;
            conn.to = ref.id + ":" + conn.to;
            result.connections.push_back(std::move(conn));
        }
    }

    loading_stack.erase(td.classname);
    return result;
}
```

### Step 2.3: Run Tests

```bash
cmake --build build -j$(nproc) && cd build && ctest -R "sub_blueprint_ref" --output-on-failure
```

All cycle detection and expansion tests should pass. All existing tests must stay green.

---

## 5. Phase 3: Persistence + Tests

### Step 3.1: Write Failing Tests

Add to `tests/test_sub_blueprint_ref.cpp`:

```cpp
#include "editor/visual/scene/persist.h"

// ============================================================
// Persistence: sub_blueprint_instances in editor format
// ============================================================

TEST(SubBlueprintPersist, SaveContainsSubBlueprintsArray) {
    Blueprint bp;

    // Top-level device
    Node bat;
    bat.id = "bat_main";
    bat.type_name = "Battery";
    bp.add_node(bat);

    // Sub-blueprint reference
    SubBlueprintInstance sbi;
    sbi.id = "lamp_1";
    sbi.blueprint_path = "library/systems/lamp_pass_through.json";
    sbi.type_name = "lamp_pass_through";
    sbi.pos = {400.0f, 300.0f};
    sbi.size = {120.0f, 80.0f};
    sbi.params_override["lamp.color"] = "green";
    bp.sub_blueprint_instances.push_back(sbi);

    // Also has internal expanded nodes (with group_id set)
    Node lamp_vin;
    lamp_vin.id = "lamp_1:vin";
    lamp_vin.type_name = "BlueprintInput";
    lamp_vin.group_id = "lamp_1";
    bp.add_node(lamp_vin);

    // Collapsed wrapper node
    Node collapsed;
    collapsed.id = "lamp_1";
    collapsed.type_name = "lamp_pass_through";
    collapsed.expandable = true;
    collapsed.collapsed = true;
    collapsed.pos = {400.0f, 300.0f};
    bp.add_node(collapsed);

    std::string json_str = blueprint_to_editor_json(bp);
    auto j = nlohmann::json::parse(json_str);

    // Should have sub_blueprints array
    ASSERT_TRUE(j.contains("sub_blueprints"));
    ASSERT_TRUE(j["sub_blueprints"].is_array());
    EXPECT_EQ(j["sub_blueprints"].size(), 1u);

    auto& sb = j["sub_blueprints"][0];
    EXPECT_EQ(sb["id"], "lamp_1");
    EXPECT_EQ(sb["blueprint_path"], "library/systems/lamp_pass_through.json");
    EXPECT_EQ(sb["type_name"], "lamp_pass_through");
    EXPECT_EQ(sb["params_override"]["lamp.color"], "green");

    // Internal nodes of referenced sub-blueprints should NOT be in devices[]
    // (they are expanded on load from the reference)
    for (const auto& dev : j["devices"]) {
        std::string name = dev["name"];
        EXPECT_FALSE(name.find("lamp_1:") != std::string::npos)
            << "Referenced sub-blueprint internal node '" << name << "' should not be in devices[]";
    }
}

TEST(SubBlueprintPersist, LoadExpandsReferences) {
    // JSON with sub_blueprints reference
    std::string json_str = R"({
        "devices": [
            {"name": "bat_main", "classname": "Battery", "pos": {"x": 100, "y": 300}}
        ],
        "sub_blueprints": [
            {
                "id": "lamp_1",
                "blueprint_path": "library/systems/lamp_pass_through.json",
                "type_name": "lamp_pass_through",
                "pos": {"x": 400, "y": 300},
                "size": {"x": 120, "y": 80}
            }
        ],
        "wires": []
    })";

    // Need a registry with lamp_pass_through defined
    an24::TypeRegistry registry;
    an24::TypeDefinition lamp;
    lamp.classname = "lamp_pass_through";
    lamp.cpp_class = false;
    an24::DeviceInstance d_vin; d_vin.name = "vin"; d_vin.classname = "BlueprintInput";
    an24::DeviceInstance d_lamp; d_lamp.name = "lamp"; d_lamp.classname = "IndicatorLight";
    lamp.devices = {d_vin, d_lamp};
    lamp.connections = {{"vin.port", "lamp.v_in", {}}};
    registry.types["lamp_pass_through"] = lamp;

    auto bp = load_blueprint_from_json(json_str, registry);
    ASSERT_TRUE(bp.has_value());

    // Should have: bat_main + lamp_1 (collapsed) + lamp_1:vin + lamp_1:lamp = 4 nodes
    EXPECT_GE(bp->nodes.size(), 3u);

    // Sub-blueprint instance metadata preserved
    EXPECT_EQ(bp->sub_blueprint_instances.size(), 1u);
    EXPECT_EQ(bp->sub_blueprint_instances[0].id, "lamp_1");

    // Internal nodes are expanded with prefix
    EXPECT_NE(bp->find_node("lamp_1:vin"), nullptr);
    EXPECT_NE(bp->find_node("lamp_1:lamp"), nullptr);
}

TEST(SubBlueprintPersist, RoundTrip_PreservesReferences) {
    // Build a blueprint with a sub-blueprint reference
    Blueprint bp;
    Node bat;
    bat.id = "bat_main";
    bat.type_name = "Battery";
    bp.add_node(bat);

    SubBlueprintInstance sbi;
    sbi.id = "lamp_1";
    sbi.blueprint_path = "library/systems/lamp_pass_through.json";
    sbi.type_name = "lamp_pass_through";
    sbi.pos = {400.0f, 300.0f};
    sbi.size = {120.0f, 80.0f};
    sbi.params_override["lamp.color"] = "green";
    bp.sub_blueprint_instances.push_back(sbi);

    // Save
    std::string json_str = blueprint_to_editor_json(bp);

    // Load (with registry for expansion)
    an24::TypeRegistry registry;
    an24::TypeDefinition lamp;
    lamp.classname = "lamp_pass_through";
    lamp.cpp_class = false;
    an24::DeviceInstance d_lamp; d_lamp.name = "lamp"; d_lamp.classname = "IndicatorLight";
    lamp.devices = {d_lamp};
    registry.types["lamp_pass_through"] = lamp;

    auto bp2 = load_blueprint_from_json(json_str, registry);
    ASSERT_TRUE(bp2.has_value());

    // Reference preserved
    ASSERT_EQ(bp2->sub_blueprint_instances.size(), 1u);
    EXPECT_EQ(bp2->sub_blueprint_instances[0].id, "lamp_1");
    EXPECT_EQ(bp2->sub_blueprint_instances[0].params_override["lamp.color"], "green");
}

TEST(SubBlueprintPersist, MixedMode_ReferencesAndBakedIn) {
    Blueprint bp;

    // Baked-in group (SubBlueprintInstance with baked_in=true, inline devices)
    Node n1;
    n1.id = "bat_grp_1:bat";
    n1.type_name = "Battery";
    n1.group_id = "bat_grp_1";
    bp.add_node(n1);

    Node collapsed1;
    collapsed1.id = "bat_grp_1";
    collapsed1.expandable = true;
    collapsed1.collapsed = true;
    collapsed1.type_name = "simple_battery";
    bp.add_node(collapsed1);

    SubBlueprintInstance baked;
    baked.id = "bat_grp_1";
    baked.type_name = "simple_battery";
    baked.baked_in = true;
    baked.internal_node_ids = {"bat_grp_1:bat"};
    bp.sub_blueprint_instances.push_back(baked);

    // Referenced sub-blueprint (baked_in=false)
    SubBlueprintInstance sbi;
    sbi.id = "lamp_1";
    sbi.type_name = "lamp_pass_through";
    sbi.blueprint_path = "library/systems/lamp_pass_through.json";
    sbi.baked_in = false;
    bp.sub_blueprint_instances.push_back(sbi);

    std::string json_str = blueprint_to_editor_json(bp);
    auto j = nlohmann::json::parse(json_str);

    // All sub-blueprints in one sub_blueprints array, distinguished by baked_in flag
    ASSERT_TRUE(j.contains("sub_blueprints"));
    EXPECT_EQ(j["sub_blueprints"].size(), 2u);

    // Find the baked-in one
    bool found_baked = false, found_ref = false;
    for (const auto& sb : j["sub_blueprints"]) {
        if (sb["id"] == "bat_grp_1") {
            EXPECT_TRUE(sb["baked_in"].get<bool>());
            found_baked = true;
        }
        if (sb["id"] == "lamp_1") {
            EXPECT_FALSE(sb.value("baked_in", false));
            found_ref = true;
        }
    }
    EXPECT_TRUE(found_baked);
    EXPECT_TRUE(found_ref);

    // Baked-in devices are in devices[], referenced are not
    bool found_baked_dev = false;
    for (const auto& dev : j["devices"]) {
        if (dev["name"] == "bat_grp_1:bat") found_baked_dev = true;
    }
    EXPECT_TRUE(found_baked_dev);
}
```

### Step 3.2: Implement Persistence Changes

#### 3.2a: `blueprint_to_editor_json()` — serialize sub_blueprint_instances

In `persist.cpp`, **replace** the existing `collapsed_groups` serialization block with unified
sub_blueprints serialization:

```cpp
// sub_blueprints (unified: baked_in flag controls inline vs reference)
json sub_blueprints_json = json::array();
std::set<std::string> referenced_group_ids;  // IDs of non-baked sub-blueprints (skip their devices)
for (const auto& sbi : bp.sub_blueprint_instances) {
    if (!sbi.baked_in) {
        referenced_group_ids.insert(sbi.id);  // referenced: skip internal devices in devices[]
    }
    json sbj;
    sbj["id"] = sbi.id;
    sbj["blueprint_path"] = sbi.blueprint_path;
    sbj["type_name"] = sbi.type_name;
    sbj["baked_in"] = sbi.baked_in;
    sbj["pos"] = {{"x", sbi.pos.x}, {"y", sbi.pos.y}};
    sbj["size"] = {{"x", sbi.size.x}, {"y", sbi.size.y}};
    if (!sbi.params_override.empty()) {
        json po;
        for (const auto& [k, v] : sbi.params_override) po[k] = v;
        sbj["params_override"] = po;
    }
    if (!sbi.layout_override.empty()) {
        json lo;
        for (const auto& [k, v] : sbi.layout_override)
            lo[k] = {{"x", v.x}, {"y", v.y}};
        sbj["layout_override"] = lo;
    }
    if (!sbi.internal_routing.empty()) {
        json ir;
        for (const auto& [k, pts] : sbi.internal_routing) {
            json arr = json::array();
            for (const auto& p : pts) arr.push_back({{"x", p.x}, {"y", p.y}});
            ir[k] = arr;
        }
        sbj["internal_routing"] = ir;
    }
    sub_blueprints_json.push_back(sbj);
}
editor["sub_blueprints"] = sub_blueprints_json;
```

**Also**: Modify the device serialization loop to **skip** nodes whose `group_id` matches a
**referenced** (non-baked) sub-blueprint:

```cpp
// In the device serialization loop:
for (const auto& n : bp.nodes) {
    // Skip internal nodes of referenced sub-blueprints (they are expanded on load)
    if (referenced_group_ids.count(n.group_id) > 0) continue;
    // Skip the collapsed wrapper node for referenced sub-blueprints
    if (n.expandable && referenced_group_ids.count(n.id) > 0) continue;
    // Baked-in internal nodes (not in referenced_group_ids) ARE serialized
    // ... existing serialization ...
}
```

#### 3.2b: `load_editor_format()` — load sub_blueprint_instances and expand

**Replace** the existing `collapsed_groups` loading block with unified sub_blueprints loading:

```cpp
// Load sub_blueprints (referenced)
if (j.contains("sub_blueprints") && j["sub_blueprints"].is_array()) {
    for (const auto& sbj : j["sub_blueprints"]) {
        SubBlueprintInstance sbi;
        sbi.id = sbj.value("id", "");
        sbi.blueprint_path = sbj.value("blueprint_path", "");
        sbi.type_name = sbj.value("type_name", "");
        sbi.baked_in = sbj.value("baked_in", false);
        if (sbj.contains("pos"))
            sbi.pos = {sbj["pos"].value("x", 0.0f), sbj["pos"].value("y", 0.0f)};
        if (sbj.contains("size"))
            sbi.size = {sbj["size"].value("x", 0.0f), sbj["size"].value("y", 0.0f)};
        if (sbj.contains("params_override")) {
            for (auto& [k, v] : sbj["params_override"].items())
                sbi.params_override[k] = v.get<std::string>();
        }
        if (sbj.contains("layout_override")) {
            for (auto& [k, v] : sbj["layout_override"].items())
                sbi.layout_override[k] = {v.value("x", 0.0f), v.value("y", 0.0f)};
        }
        if (sbj.contains("internal_routing")) {
            for (auto& [k, v] : sbj["internal_routing"].items()) {
                std::vector<Pt> pts;
                for (const auto& p : v)
                    pts.push_back({p.value("x", 0.0f), p.value("y", 0.0f)});
                sbi.internal_routing[k] = pts;
            }
        }
        bp.sub_blueprint_instances.push_back(sbi);

        // Only expand references (baked_in devices are already in devices[])
        if (!sbi.baked_in) {
            expand_sub_blueprint_on_load(bp, sbi, registry);
        }
    }
}
```

#### 3.2c: `expand_sub_blueprint_on_load()` helper

New function in persist.cpp:

```cpp
static void expand_sub_blueprint_on_load(
    Blueprint& bp,
    const SubBlueprintInstance& sbi,
    const an24::TypeRegistry& registry)
{
    const auto* td = registry.get(sbi.type_name);
    if (!td) {
        spdlog::error("Sub-blueprint '{}' not found in registry", sbi.type_name);
        return;
    }

    // Expand type definition to get internal nodes + wires
    Blueprint sub_bp = expand_type_definition(*td, registry);

    std::vector<std::string> internal_node_ids;
    for (auto& node : sub_bp.nodes) {
        node.id = sbi.id + ":" + node.id;
        node.name = node.id;
        node.group_id = sbi.id;

        // Apply layout override
        auto lay_it = sbi.layout_override.find(
            node.id.substr(sbi.id.size() + 1));  // unprefixed name
        if (lay_it != sbi.layout_override.end()) {
            node.pos = lay_it->second;
        }

        internal_node_ids.push_back(node.id);
        bp.add_node(std::move(node));
    }

    for (auto& wire : sub_bp.wires) {
        wire.start.node_id = sbi.id + ":" + wire.start.node_id;
        wire.end.node_id = sbi.id + ":" + wire.end.node_id;
        wire.id = sbi.id + ":" + wire.id;

        // Apply routing override
        std::string wire_key = wire.start.node_id.substr(sbi.id.size() + 1) + "." +
                               wire.start.port_name + "->" +
                               wire.end.node_id.substr(sbi.id.size() + 1) + "." +
                               wire.end.port_name;
        auto rt_it = sbi.internal_routing.find(wire_key);
        if (rt_it != sbi.internal_routing.end()) {
            wire.routing_points = rt_it->second;
        }

        bp.add_wire(std::move(wire));
    }

    // Create collapsed wrapper node
    Node collapsed;
    collapsed.id = sbi.id;
    collapsed.name = sbi.id;
    collapsed.type_name = sbi.type_name;
    collapsed.expandable = true;
    collapsed.collapsed = true;
    collapsed.pos = sbi.pos;
    collapsed.size = sbi.size;
    // Copy exposed ports from type definition
    for (const auto& [pname, port] : td->ports) {
        if (port.direction == an24::PortDirection::In)
            collapsed.inputs.emplace_back(pname.c_str(), PortSide::Input, port.type);
        else
            collapsed.outputs.emplace_back(pname.c_str(), PortSide::Output, port.type);
    }
    bp.add_node(collapsed);
}
```

> **Signature change:** `load_editor_format()` currently doesn't take a TypeRegistry.
> Change to a **single signature** with mandatory `TypeRegistry` parameter:
>
> ```cpp
> // Single signature — all callers must provide a TypeRegistry
> std::optional<Blueprint> load_editor_format(const json& j, const TypeRegistry& registry);
> ```
>
> Tests create a minimal `TypeRegistry` with just the types needed for the test.
> Same pattern for `load_blueprint_from_file()` and `load_blueprint_from_json()`.

### Step 3.3: Run Tests

```bash
cmake --build build -j$(nproc) && cd build && ctest --output-on-failure
```

---

## 6. Phase 4: Bake-In + Tests

### Step 4.1: Write Failing Tests (test_bake_in.cpp)

Create `tests/test_bake_in.cpp`. Register in `tests/CMakeLists.txt`.

```cpp
#include <gtest/gtest.h>
#include "editor/data/blueprint.h"
#include "editor/document.h"
#include "json_parser/json_parser.h"

// ============================================================
// Bake-In: Convert SubBlueprintInstance from reference to embedded
// ============================================================

TEST(BakeIn, SetsBakedInFlag) {
    // Setup: Create a blueprint with a sub-blueprint reference (already expanded in memory)
    Blueprint bp;

    // Top-level device
    Node bat;
    bat.id = "bat_main";
    bat.type_name = "Battery";
    bp.add_node(bat);

    // Internal nodes of reference (already expanded)
    Node vin;
    vin.id = "lamp_1:vin";
    vin.type_name = "BlueprintInput";
    vin.group_id = "lamp_1";
    bp.add_node(vin);

    Node lamp;
    lamp.id = "lamp_1:lamp";
    lamp.type_name = "IndicatorLight";
    lamp.group_id = "lamp_1";
    lamp.params["color"] = "red";
    bp.add_node(lamp);

    // Collapsed wrapper
    Node collapsed;
    collapsed.id = "lamp_1";
    collapsed.type_name = "lamp_pass_through";
    collapsed.expandable = true;
    collapsed.collapsed = true;
    collapsed.pos = {400.0f, 300.0f};
    collapsed.size = {120.0f, 80.0f};
    bp.add_node(collapsed);

    // Sub-blueprint reference (baked_in=false)
    SubBlueprintInstance sbi;
    sbi.id = "lamp_1";
    sbi.blueprint_path = "library/systems/lamp_pass_through.json";
    sbi.type_name = "lamp_pass_through";
    sbi.pos = {400.0f, 300.0f};
    sbi.size = {120.0f, 80.0f};
    sbi.baked_in = false;
    sbi.params_override["lamp.color"] = "green";
    bp.sub_blueprint_instances.push_back(sbi);

    // Act: bake in
    bool result = bp.bake_in_sub_blueprint("lamp_1");
    ASSERT_TRUE(result);

    // SubBlueprintInstance still present but now baked_in=true
    ASSERT_EQ(bp.sub_blueprint_instances.size(), 1u);
    EXPECT_TRUE(bp.sub_blueprint_instances[0].baked_in);
    EXPECT_EQ(bp.sub_blueprint_instances[0].id, "lamp_1");
    EXPECT_EQ(bp.sub_blueprint_instances[0].blueprint_path,
              "library/systems/lamp_pass_through.json");  // preserved for origin info

    // Overrides cleared (already flattened into nodes)
    EXPECT_TRUE(bp.sub_blueprint_instances[0].params_override.empty());
    EXPECT_TRUE(bp.sub_blueprint_instances[0].layout_override.empty());
    EXPECT_TRUE(bp.sub_blueprint_instances[0].internal_routing.empty());

    // internal_node_ids populated
    EXPECT_EQ(bp.sub_blueprint_instances[0].internal_node_ids.size(), 2u);

    // Internal nodes still present
    EXPECT_NE(bp.find_node("lamp_1:vin"), nullptr);
    EXPECT_NE(bp.find_node("lamp_1:lamp"), nullptr);

    // Collapsed wrapper node still present and unchanged
    auto* cnode = bp.find_node("lamp_1");
    ASSERT_NE(cnode, nullptr);
    EXPECT_TRUE(cnode->expandable);
}

TEST(BakeIn, FlattensParamOverrides) {
    Blueprint bp;

    Node lamp;
    lamp.id = "lamp_1:lamp";
    lamp.type_name = "IndicatorLight";
    lamp.group_id = "lamp_1";
    lamp.params["color"] = "red";  // Default from library
    bp.add_node(lamp);

    Node collapsed;
    collapsed.id = "lamp_1";
    collapsed.expandable = true;
    collapsed.collapsed = true;
    bp.add_node(collapsed);

    SubBlueprintInstance sbi;
    sbi.id = "lamp_1";
    sbi.type_name = "lamp_pass_through";
    sbi.params_override["lamp.color"] = "green";  // Override
    bp.sub_blueprint_instances.push_back(sbi);

    bp.bake_in_sub_blueprint("lamp_1");

    // Override should be permanently applied to the device
    auto* lamp_node = bp.find_node("lamp_1:lamp");
    ASSERT_NE(lamp_node, nullptr);
    EXPECT_EQ(lamp_node->params["color"], "green");
}

TEST(BakeIn, FlattensLayoutOverrides) {
    Blueprint bp;

    Node vin;
    vin.id = "lamp_1:vin";
    vin.type_name = "BlueprintInput";
    vin.group_id = "lamp_1";
    vin.pos = {0.0f, 0.0f};  // Default position
    bp.add_node(vin);

    Node collapsed;
    collapsed.id = "lamp_1";
    collapsed.expandable = true;
    collapsed.collapsed = true;
    bp.add_node(collapsed);

    SubBlueprintInstance sbi;
    sbi.id = "lamp_1";
    sbi.type_name = "lamp_pass_through";
    sbi.layout_override["vin"] = {350.0f, 300.0f};  // Override position
    bp.sub_blueprint_instances.push_back(sbi);

    bp.bake_in_sub_blueprint("lamp_1");

    auto* vin_node = bp.find_node("lamp_1:vin");
    ASSERT_NE(vin_node, nullptr);
    EXPECT_FLOAT_EQ(vin_node->pos.x, 350.0f);
    EXPECT_FLOAT_EQ(vin_node->pos.y, 300.0f);
}

TEST(BakeIn, NonexistentId_ReturnsFalse) {
    Blueprint bp;
    EXPECT_FALSE(bp.bake_in_sub_blueprint("nonexistent"));
}

TEST(BakeIn, SaveAfterBakeIn_InlineDevicesInJson) {
    Blueprint bp;

    Node vin;
    vin.id = "lamp_1:vin";
    vin.type_name = "BlueprintInput";
    vin.group_id = "lamp_1";
    bp.add_node(vin);

    Node collapsed;
    collapsed.id = "lamp_1";
    collapsed.type_name = "lamp_pass_through";
    collapsed.expandable = true;
    collapsed.collapsed = true;
    bp.add_node(collapsed);

    // Baked-in SubBlueprintInstance (baked_in=true)
    SubBlueprintInstance sbi;
    sbi.id = "lamp_1";
    sbi.type_name = "lamp_pass_through";
    sbi.baked_in = true;
    sbi.internal_node_ids = {"lamp_1:vin"};
    bp.sub_blueprint_instances.push_back(sbi);

    std::string json_str = blueprint_to_editor_json(bp);
    auto j = nlohmann::json::parse(json_str);

    // Baked-in devices are in devices[]
    bool found = false;
    for (const auto& d : j["devices"]) {
        if (d["name"] == "lamp_1:vin") found = true;
    }
    EXPECT_TRUE(found);

    // sub_blueprints entry present with baked_in: true
    ASSERT_TRUE(j.contains("sub_blueprints"));
    ASSERT_EQ(j["sub_blueprints"].size(), 1u);
    EXPECT_TRUE(j["sub_blueprints"][0]["baked_in"].get<bool>());
}
```

### Step 4.2: Implement Bake-In

#### 4.2a: `Blueprint::bake_in_sub_blueprint()` in `blueprint.h` / `blueprint.cpp`

Declare in `blueprint.h`:

```cpp
    /// Convert a SubBlueprintInstance from reference (baked_in=false) to embedded (baked_in=true).
    /// Merges overrides into actual node data, sets baked_in flag, clears overrides.
    /// Returns false if id not found in sub_blueprint_instances or already baked in.
    bool bake_in_sub_blueprint(const std::string& id);
```

Implement in `blueprint.cpp`:

```cpp
bool Blueprint::bake_in_sub_blueprint(const std::string& id) {
    // Find the SubBlueprintInstance
    auto sbi_it = std::find_if(sub_blueprint_instances.begin(), sub_blueprint_instances.end(),
                                [&](const SubBlueprintInstance& s) { return s.id == id; });
    if (sbi_it == sub_blueprint_instances.end()) return false;

    const SubBlueprintInstance& sbi = *sbi_it;
    std::string prefix = sbi.id + ":";

    // 1. Apply param overrides permanently
    for (const auto& [override_key, override_val] : sbi.params_override) {
        auto dot = override_key.find('.');
        if (dot == std::string::npos) continue;
        std::string dev_name = override_key.substr(0, dot);
        std::string param_name = override_key.substr(dot + 1);
        std::string target_id = sbi.id + ":" + dev_name;

        Node* node = find_node(target_id.c_str());
        if (node) {
            node->params[param_name] = override_val;
        }
    }

    // 2. Apply layout overrides permanently
    for (const auto& [dev_name, pos] : sbi.layout_override) {
        std::string target_id = sbi.id + ":" + dev_name;
        Node* node = find_node(target_id.c_str());
        if (node) {
            node->pos = pos;
        }
    }

    // 3. Apply routing overrides permanently
    for (const auto& [wire_key, routing] : sbi.internal_routing) {
        for (auto& wire : wires) {
            // Match wire by key pattern
            std::string wk = wire.start.node_id.substr(prefix.size()) + "." +
                             wire.start.port_name + "->" +
                             wire.end.node_id.substr(prefix.size()) + "." +
                             wire.end.port_name;
            if (wk == wire_key) {
                wire.routing_points = routing;
                break;
            }
        }
    }

    // 4. Collect internal node IDs
    std::vector<std::string> internal_ids;
    for (const auto& n : nodes) {
        if (n.group_id == sbi.id && n.id != sbi.id) {
            internal_ids.push_back(n.id);
        }
    }

    // 5. Set baked_in flag and clear overrides (already flattened into nodes)
    sbi_it->baked_in = true;
    sbi_it->internal_node_ids = internal_ids;
    sbi_it->params_override.clear();
    sbi_it->layout_override.clear();
    sbi_it->internal_routing.clear();
    // blueprint_path preserved for origin tracking

    return true;
}
```

### Step 4.3: Run Tests

```bash
cmake --build build -j$(nproc) && cd build && ctest --output-on-failure
```

---

## 7. Phase 5: Editor Integration + Tests

### Step 5.1: Context Menu — Bake In Button

#### Current context menu flow

```
an24_editor.cpp:
  if (ws.nodeContextMenu.show) {
      ImGui::OpenPopup("NodeContextMenu");
  }
  if (ImGui::BeginPopup("NodeContextMenu")) {
      // "Properties", "Set Color", "Delete"
      ImGui::EndPopup();
  }
```

#### Add "Bake In (Embed)" menu item

In `examples/an24_editor.cpp`, inside the `NodeContextMenu` popup rendering:

```cpp
if (ImGui::BeginPopup("NodeContextMenu")) {
    auto* doc = ws.nodeContextMenu.source_doc;
    int idx = ws.nodeContextMenu.node_index;
    if (doc && idx >= 0 && idx < (int)doc->scene().nodes().size()) {
        const auto& node = doc->scene().nodes()[idx];

        if (ImGui::MenuItem("Properties")) {
            ws.openPropertiesForNode(idx, doc);
        }
        if (ImGui::MenuItem("Set Color...")) {
            // ... existing ...
        }

        // NEW: Bake In option — only for expandable nodes that are non-baked references
        if (node.expandable) {
            auto* sbi = doc->scene().blueprint().find_sub_blueprint_instance(node.id);
            if (sbi && !sbi->baked_in) {
                ImGui::Separator();
                if (ImGui::MenuItem("Bake In (Embed)")) {
                    // Show confirmation in next frame (can't show modal inside popup)
                    ws.pendingBakeIn.show_confirmation = true;
                    ws.pendingBakeIn.doc = doc;
                    ws.pendingBakeIn.sub_blueprint_id = node.id;
                }
            }
        }

        ImGui::Separator();
        if (ImGui::MenuItem("Delete")) {
            // ... existing ...
        }
    }
    ImGui::EndPopup();
}
```

#### Add confirmation dialog

```cpp
// Bake-in confirmation dialog (after popup ends)
if (ws.pendingBakeIn.show_confirmation) {
    ImGui::OpenPopup("ConfirmBakeIn");
    ws.pendingBakeIn.show_confirmation = false;
}
if (ImGui::BeginPopupModal("ConfirmBakeIn", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Permanently embed sub-blueprint '%s'?",
                ws.pendingBakeIn.sub_blueprint_id.c_str());
    ImGui::Text("Changes to the library file will no longer affect this instance.");
    ImGui::Separator();
    if (ImGui::Button("Bake In", ImVec2(120, 0))) {
        auto* doc = ws.pendingBakeIn.doc;
        if (doc) {
            doc->scene().blueprint().bake_in_sub_blueprint(
                ws.pendingBakeIn.sub_blueprint_id);
            doc->scene().cache().clear();
            doc->markModified();
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}
```

#### Add pending bake-in state to WindowSystem

In `src/editor/window_system.h`:

```cpp
struct PendingBakeIn {
    bool show_confirmation = false;
    Document* doc = nullptr;
    std::string sub_blueprint_id;
} pendingBakeIn;
```

### Step 5.2: Document::addBlueprint() — Create Reference Instead of Inline

The current `Document::addBlueprint()` immediately expands everything inline. Modify it to:

1. Create `SubBlueprintInstance` instead of `CollapsedGroup`
2. Still expand internal nodes (needed for visual display)
3. Track via `sub_blueprint_instances` instead of `collapsed_groups`

**Changes to `document.cpp` addBlueprint():**

Replace the CollapsedGroup creation block (step 7 in current code) with:

```cpp
    // 7. CREATE SUB-BLUEPRINT INSTANCE (reference, not baked-in)
    SubBlueprintInstance sbi;
    sbi.id = unique_id;
    sbi.blueprint_path = blueprint_name; // classname as path for registry lookup
    sbi.type_name = blueprint_name;
    sbi.pos = snapped_pos;
    sbi.size = Pt(120.0f, height);
    sbi.baked_in = false;
    scene().blueprint().sub_blueprint_instances.push_back(sbi);
```

#### Update `recompute_group_ids()` — single vector only

In `blueprint.h`, modify `recompute_group_ids()` to use only `sub_blueprint_instances`:

```cpp
void recompute_group_ids() {
    std::unordered_map<std::string, std::string> node_to_group;

    // All groups (both baked-in and referenced) are in sub_blueprint_instances
    for (const auto& sbi : sub_blueprint_instances) {
        std::string prefix = sbi.id + ":";
        for (const auto& n : nodes) {
            if (n.id.size() > prefix.size() &&
                n.id.substr(0, prefix.size()) == prefix) {
                node_to_group[n.id] = sbi.id;
            }
        }
    }

    for (auto& n : nodes) {
        auto it = node_to_group.find(n.id);
        if (it != node_to_group.end()) {
            n.group_id = it->second;
        } else if (!n.expandable) {
            n.group_id = "";
        }
    }
}
```

### Step 5.3: Run All Tests

```bash
cmake --build build -j$(nproc) && cd build && ctest --output-on-failure
```

**Critical**: Run `test_editor_hierarchical` explicitly — these 23 tests may need updates
since addBlueprint now creates SubBlueprintInstance instead of CollapsedGroup.

Tests that check `collapsed_groups.size()` must be changed to check
`sub_blueprint_instances.size()` and verify `baked_in` flag as appropriate.
Review each failure and update accordingly.

---

## 8. Phase 6: Cleanup

### Step 6.1: Update All Existing Tests

Run full test suite. For each failure:

- If test checks `collapsed_groups` → update to check `sub_blueprint_instances` with `baked_in` flag
- If test checks `internal_node_ids` → access via `sub_blueprint_instances[i].internal_node_ids`
- If save/load roundtrip test → verify new format with `sub_blueprints` field and `baked_in` flag

### Step 6.2: Remove Dead Code

- **Delete `CollapsedGroup` struct** from `blueprint.h` — fully replaced by `SubBlueprintInstance`
- **Delete `collapsed_groups` vector** from `Blueprint` — replaced by `sub_blueprint_instances`
- Remove any `collapsed_groups` serialization code from `persist.cpp`
- Remove any `ParserContext.templates` usage if fully replaced
- **Reuse `Node::blueprint_path`** — already exists and serialized by persist.cpp. Set it to the
  sub-blueprint's `type_name` on the collapsed wrapper node. No new field needed.

### Step 6.3: Update Library JSON Files

If any library JSON files have inline expanded devices that should be references:

- Convert `devices[]` with prefixed names → `sub_blueprints[]` references
- Test library loading

---

## 9. Phase 7: Hierarchical AOT Codegen + Tests

### Overview

Extend `src/codegen/codegen.cpp` to generate `Systems` classes for composites (`cpp_class: false`).
A composite's `Systems` contains sub-composites as **nested `Systems` fields** (not flattened),
preserving the hierarchy for cache locality and full compiler optimization.

```
power_distribution_Systems
├── battery_bank_1: battery_bank_Systems     (nested Systems field)
│     └── sb_1: simple_battery_Systems
│           ├── bat: Battery<AotProvider>
│           └── vin: BlueprintInput<AotProvider>
├── lamp_1: lamp_pass_through_Systems        (nested Systems field)
│     ├── vin: BlueprintInput<AotProvider>
│     ├── lamp: IndicatorLight<AotProvider>
│     └── vout: BlueprintOutput<AotProvider>
└── bus_main: Bus<AotProvider>               (top-level primitive)
```

### Step 7.1: Write Failing Tests (test_aot_composite.cpp)

Create `tests/test_aot_composite.cpp`. Register in `tests/CMakeLists.txt`.

```cpp
#include <gtest/gtest.h>
#include "codegen/codegen.h"
#include "json_parser/json_parser.h"

using namespace an24;

// ============================================================
// Composite Systems generation
// ============================================================

TEST(AotComposite, GeneratesSystemsForComposite) {
    // Setup: simple composite with 2 devices
    TypeRegistry registry;

    TypeDefinition lamp;
    lamp.classname = "lamp_pass_through";
    lamp.cpp_class = false;
    DeviceInstance d_vin;  d_vin.name = "vin";  d_vin.classname = "BlueprintInput";
    DeviceInstance d_lamp; d_lamp.name = "lamp"; d_lamp.classname = "IndicatorLight";
    DeviceInstance d_vout; d_vout.name = "vout"; d_vout.classname = "BlueprintOutput";
    lamp.devices = {d_vin, d_lamp, d_vout};
    lamp.connections = {{"vin.port", "lamp.v_in", {}}, {"lamp.v_out", "vout.port", {}}};
    registry.types["lamp_pass_through"] = lamp;

    // Generate code
    auto result = generate_composite_systems(lamp, registry);

    // Should produce header + source
    EXPECT_FALSE(result.header.empty());
    EXPECT_FALSE(result.source.empty());

    // Header should contain class name
    EXPECT_NE(result.header.find("lamp_pass_through_Systems"), std::string::npos);

    // Should contain device fields (primitive devices as AotProvider fields)
    EXPECT_NE(result.header.find("BlueprintInput"), std::string::npos);
    EXPECT_NE(result.header.find("IndicatorLight"), std::string::npos);
    EXPECT_NE(result.header.find("BlueprintOutput"), std::string::npos);

    // Should have solve_step and pre_load
    EXPECT_NE(result.header.find("solve_step"), std::string::npos);
    EXPECT_NE(result.header.find("pre_load"), std::string::npos);
}

TEST(AotComposite, NestedComposite_ContainsSubSystems) {
    TypeRegistry registry;

    // Inner composite
    TypeDefinition inner;
    inner.classname = "simple_battery";
    inner.cpp_class = false;
    DeviceInstance d_bat; d_bat.name = "bat"; d_bat.classname = "Battery";
    inner.devices = {d_bat};
    registry.types["simple_battery"] = inner;

    // Outer composite references inner
    TypeDefinition outer;
    outer.classname = "battery_bank";
    outer.cpp_class = false;
    SubBlueprintRef ref;
    ref.id = "sb_1";
    ref.type_name = "simple_battery";
    outer.sub_blueprints.push_back(ref);
    DeviceInstance d_bus; d_bus.name = "bus"; d_bus.classname = "Bus";
    outer.devices = {d_bus};
    registry.types["battery_bank"] = outer;

    auto result = generate_composite_systems(outer, registry);

    // Should reference the inner Systems class, NOT flatten
    EXPECT_NE(result.header.find("simple_battery_Systems"), std::string::npos)
        << "Nested composite should appear as Systems field, not flattened";

    // Should also have the top-level primitive
    EXPECT_NE(result.header.find("Bus"), std::string::npos);
}

TEST(AotComposite, ThreeLevelsDeep_FullHierarchy) {
    TypeRegistry registry;

    // Level 2: leaf
    TypeDefinition leaf;
    leaf.classname = "leaf_type";
    leaf.cpp_class = false;
    DeviceInstance d_r; d_r.name = "r1"; d_r.classname = "Resistor";
    leaf.devices = {d_r};
    registry.types["leaf_type"] = leaf;

    // Level 1: mid references leaf
    TypeDefinition mid;
    mid.classname = "mid_type";
    mid.cpp_class = false;
    SubBlueprintRef ref_leaf;
    ref_leaf.id = "leaf_inst";
    ref_leaf.type_name = "leaf_type";
    mid.sub_blueprints.push_back(ref_leaf);
    registry.types["mid_type"] = mid;

    // Level 0: top references mid
    TypeDefinition top;
    top.classname = "top_type";
    top.cpp_class = false;
    SubBlueprintRef ref_mid;
    ref_mid.id = "mid_inst";
    ref_mid.type_name = "mid_type";
    top.sub_blueprints.push_back(ref_mid);
    registry.types["top_type"] = top;

    auto result = generate_composite_systems(top, registry);

    // Should have nested hierarchy: top → mid_Systems → leaf_Systems
    EXPECT_NE(result.header.find("mid_type_Systems"), std::string::npos);
    // NOTE: leaf_type_Systems is inside mid_type_Systems, not directly in top
    // The #include for leaf should be present
    EXPECT_NE(result.header.find("leaf_type"), std::string::npos);
}

// ============================================================
// Topological ordering
// ============================================================

TEST(AotComposite, TopoSort_LeavesFirst) {
    TypeRegistry registry;

    TypeDefinition leaf;
    leaf.classname = "leaf";
    leaf.cpp_class = false;
    leaf.devices = {{.name = "d", .classname = "Battery"}};
    registry.types["leaf"] = leaf;

    TypeDefinition parent;
    parent.classname = "parent";
    parent.cpp_class = false;
    SubBlueprintRef ref;
    ref.id = "l1";
    ref.type_name = "leaf";
    parent.sub_blueprints.push_back(ref);
    registry.types["parent"] = parent;

    auto order = registry.get_composites_topo_sorted();

    // leaf must come before parent
    auto it_leaf = std::find(order.begin(), order.end(), "leaf");
    auto it_parent = std::find(order.begin(), order.end(), "parent");
    ASSERT_NE(it_leaf, order.end());
    ASSERT_NE(it_parent, order.end());
    EXPECT_LT(std::distance(order.begin(), it_leaf),
              std::distance(order.begin(), it_parent));
}

// ============================================================
// JIT vs AOT equivalence for composites
// ============================================================

TEST(AotComposite, OutputMatchesJitExpansion) {
    // This test will require both JIT and AOT to run the same composite
    // and compare output signals. Detailed implementation depends on
    // SimulationState setup — placeholder for now.
    //
    // Setup:
    //   1. Create composite with known devices + connections
    //   2. Run N steps via JIT (expand_sub_blueprint_references + JIT solver)
    //   3. Compile AOT Systems (or use pre-generated)
    //   4. Run N steps via AOT
    //   5. Compare all signal values within tolerance
    //
    // EXPECT_NEAR(jit_signals[i], aot_signals[i], 1e-6f);
    GTEST_SKIP() << "Requires compiled AOT output — implement after code generation works";
}
```

### Step 7.2: Implement Hierarchical AOT

#### 7.2a: New API in `src/codegen/codegen.h`

```cpp
/// Result of composite code generation
struct CompositeCodegenResult {
    std::string header;   // .gen.h content
    std::string source;   // .gen.cpp content
    std::string class_name;  // e.g. "lamp_pass_through_Systems"
};

/// Generate Systems class for a composite blueprint.
/// Recursively generates nested Systems for sub-composites.
/// Throws on cycles. Registry must contain all referenced types.
CompositeCodegenResult generate_composite_systems(
    const TypeDefinition& td,
    const TypeRegistry& registry);

/// Generate all composites in topological order (leaves first).
/// Returns map: classname → {header, source}.
std::map<std::string, CompositeCodegenResult> generate_all_composites(
    const TypeRegistry& registry);
```

#### 7.2b: Topo sort in `TypeRegistry`

```cpp
std::vector<std::string> TypeRegistry::get_composites_topo_sorted() const {
    std::vector<std::string> result;
    std::set<std::string> visited;
    std::set<std::string> in_stack;  // cycle detection

    std::function<void(const std::string&)> visit = [&](const std::string& name) {
        if (visited.count(name)) return;
        if (in_stack.count(name))
            throw std::runtime_error("Cycle in composite hierarchy: " + name);
        in_stack.insert(name);

        auto it = types.find(name);
        if (it == types.end() || it->second.cpp_class) return;  // skip primitives

        for (const auto& ref : it->second.sub_blueprints) {
            visit(ref.type_name);
        }

        in_stack.erase(name);
        visited.insert(name);
        result.push_back(name);
    };

    for (const auto& [name, td] : types) {
        if (!td.cpp_class) visit(name);
    }
    return result;
}
```

#### 7.2c: `generate_composite_systems()` in `codegen.cpp`

High-level algorithm:

```cpp
CompositeCodegenResult generate_composite_systems(
    const TypeDefinition& td,
    const TypeRegistry& registry)
{
    std::string class_name = td.classname + "_Systems";
    std::ostringstream header, source;

    // 1. Allocate signal indices for all internal connections
    SignalAllocator signals;
    for (const auto& dev : td.devices) {
        // Allocate signals for each port of each primitive device
        signals.allocate(dev);
    }
    for (const auto& ref : td.sub_blueprints) {
        // Allocate signals for exposed ports of sub-composite
        const auto* sub_td = registry.get(ref.type_name);
        signals.allocate_exposed(ref.id, *sub_td);
    }
    signals.wire(td.connections);  // Connect matching signal indices

    // 2. Generate header
    header << "#pragma once\n";
    header << "#include \"jit_solver/components/all_components.h\"\n";
    // Include headers for sub-composite Systems
    for (const auto& ref : td.sub_blueprints) {
        header << "#include \"" << ref.type_name << "_systems.gen.h\"\n";
    }
    header << "\nnamespace an24 {\n\n";
    header << "class " << class_name << " {\n";

    // Sub-composite fields (nested Systems)
    for (const auto& ref : td.sub_blueprints) {
        const auto* sub_td = registry.get(ref.type_name);
        if (!sub_td->cpp_class) {
            header << "    " << ref.type_name << "_Systems " << ref.id << ";\n";
        }
    }

    // Primitive device fields (AotProvider)
    for (const auto& dev : td.devices) {
        header << "    " << generate_aot_field(dev, signals) << ";\n";
    }

    header << "\n    static constexpr uint32_t SIGNAL_COUNT = "
           << signals.count() << ";\n";
    header << "\npublic:\n";
    header << "    void solve_step(void* state, uint32_t step, float dt);\n";
    header << "    void pre_load();\n";
    header << "};\n\n";
    header << "} // namespace an24\n";

    // 3. Generate source with step functions + jump table
    source << "#include \"" << td.classname << "_systems.gen.h\"\n\n";
    source << "namespace an24 {\n\n";
    // Generate step_N() methods — same 60-step LCM scheduling
    // Each step: wire signals → call sub-composite solve_step() → call primitive solve_*()
    generate_step_functions(source, td, registry, signals, class_name);
    // Generate jump table dispatch
    generate_jump_table(source, class_name);
    source << "} // namespace an24\n";

    return {header.str(), source.str(), class_name};
}
```

> **Key difference from flat codegen**: sub-composites are NOT expanded into the parent's
> device list. Instead, `sub_composite.solve_step()` is called as a single unit.
> Signal wiring between parent and sub-composite happens before/after the call.

#### 7.2d: Signal Wiring Between Levels

The parent's `step_N()` method handles port-to-signal mapping:

```cpp
// In generated step_0():
void power_distribution_Systems::step_0(void* state, float dt) {
    auto& st = *static_cast<SimulationState*>(state);

    // === Wire parent signals → sub-composite input ports ===
    // lamp_1.vin connects to bus_main.v
    st.across[lamp_1_base + lamp_pass_through_Systems::SIG_VIN_PORT]
        = st.across[SIG_BUS_MAIN_V];

    // === Solve sub-composite (entire hierarchy in one call) ===
    lamp_1.solve_step(state, 0, dt);

    // === Wire sub-composite output ports → parent signals ===
    st.across[SIG_LAMP_1_VOUT]
        = st.across[lamp_1_base + lamp_pass_through_Systems::SIG_VOUT_PORT];

    // === Solve top-level primitives ===
    bus_main.solve_electrical(st, dt);
}
```

**Signal index strategy**: each `Systems` instance has a `base_signal_offset` assigned at
construction by the parent. All internal signals are `base + local_index`.

### Step 7.3: Run Tests

```bash
cmake --build build -j$(nproc) && cd build && ctest -R "aot_composite" --output-on-failure
```

Also run the existing AOT tests and benchmarks to ensure no regression:

```bash
ctest -R "codegen|aot" --output-on-failure
```

### Step 7.4: Benchmark

Add to `examples/benchmark_jit_vs_aot.cpp`:

```cpp
// Composite benchmark: lamp_pass_through (small composite)
// Compare JIT (expand + variant dispatch) vs AOT (nested Systems)
{
    auto jit_time = benchmark_jit_composite("lamp_pass_through", registry, steps);
    auto aot_time = benchmark_aot_composite(lamp_pass_through_Systems{}, steps);
    printf("lamp_pass_through: JIT %.2f ms, AOT %.2f ms (%.1fx)\n",
           jit_time, aot_time, jit_time / aot_time);
}

// Deep composite: power_distribution (3 levels)
{
    auto jit_time = benchmark_jit_composite("power_distribution", registry, steps);
    auto aot_time = benchmark_aot_composite(power_distribution_Systems{}, steps);
    printf("power_distribution: JIT %.2f ms, AOT %.2f ms (%.1fx)\n",
           jit_time, aot_time, jit_time / aot_time);
}
```

---

## 10. Verification Checklist

### After Each Phase

- [ ] `cmake --build build -j$(nproc)` — clean compile, zero warnings
- [ ] `cd build && ctest --output-on-failure` — all tests pass
- [ ] `git add -A && git commit` — checkpoint after each phase

### Final Verification

- [ ] Create a blueprint with 2 sub-blueprint references → save → close → reopen → verify references intact
- [ ] Expand sub-blueprint (double-click) → see internal nodes in sub-window
- [ ] Edit internal param → save → verify params_override in JSON
- [ ] Bake In via context menu → confirm dialog → save → verify inline in JSON
- [ ] Mixed mode: one `baked_in=false` + one `baked_in=true` in same file → save/load roundtrip
- [ ] Cycle detection: A references B, B references A → error on load
- [ ] 3-level deep nesting → correct prefix chains (a:b:c:device)
- [ ] Delete a referenced sub-blueprint → proper cleanup
- [ ] Simulator export (blueprint_to_json) → flat devices with ext alias rewriting
- [ ] AOT composite: generated `Systems` for `lamp_pass_through` compiles and runs
- [ ] AOT hierarchical: 3-level composite → nested `Systems` classes, correct signal wiring
- [ ] AOT vs JIT: same composite produces identical output signals within tolerance
- [ ] AOT benchmark: composite AOT faster than composite JIT

### Performance

- [ ] Load time with 10+ sub-blueprints < 100ms
- [ ] Save time unchanged (references are small)
- [ ] No dynamic allocation in simulation hot path (unchanged)

---

## Summary of New/Modified Files

```
NEW:
  tests/test_sub_blueprint_ref.cpp    — ~200 lines, 15+ tests
  tests/test_bake_in.cpp              — ~150 lines, 5+ tests
  tests/test_aot_composite.cpp        — ~200 lines, 6+ tests

MODIFIED:
  src/json_parser/json_parser.h       — SubBlueprintRef struct, TypeDefinition.sub_blueprints
  src/json_parser/json_parser.cpp     — parse_type_definition(), expand_sub_blueprint_references()
  src/editor/data/blueprint.h         — Remove CollapsedGroup, add SubBlueprintInstance with baked_in,
                                         single sub_blueprint_instances vector, find/remove/bake_in methods
  src/editor/data/blueprint.cpp       — find/remove/bake_in implementations
  src/editor/visual/scene/persist.cpp — save/load sub_blueprint_instances (baked_in flag), expand on load
  src/editor/document.cpp             — addBlueprint() creates SubBlueprintInstance (baked_in=false)
  src/codegen/codegen.h               — generate_composite_systems() API, CompositeCodegenResult
  src/codegen/codegen.cpp             — hierarchical AOT: recursive codegen, signal wiring, topo sort
  src/editor/window_system.h          — PendingBakeIn state
  examples/an24_editor.cpp            — "Bake In" context menu + confirmation dialog
  examples/benchmark_jit_vs_aot.cpp   — composite AOT benchmarks
  tests/CMakeLists.txt                — add new test targets
  tests/test_editor_hierarchical.cpp  — update: collapsed_groups → sub_blueprint_instances + baked_in flag
```

## Phase Execution Order

```
Phase 1: Data Structures             │ 6 tests
Phase 2: Recursive Loading            │ 6 tests
Phase 3: Persistence                  │ 4 tests
Phase 4: Bake-In                      │ 5 tests
Phase 5: Editor Integration           │ update existing tests
Phase 6: Cleanup                      │ 0 new tests
Phase 7: Hierarchical AOT Codegen     │ 6+ tests
──────────────────────────────────────────────
Total                                 │ 27+ new tests
```

Each phase: **write failing tests → implement → green → commit**.
