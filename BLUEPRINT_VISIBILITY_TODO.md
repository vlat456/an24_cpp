# Architecture Review: Hierarchical Blueprint Visibility System

## Context for AI Agent

This document describes a **fundamental architectural problem** in the AN-24 flight simulator editor. You are being asked to:

1. **Review the current approach** - Is visibility-based filtering the right solution?
2. **Identify all problems** - Not just the bugs, but architectural issues
3. **Propose a solution** - You may suggest ANY approach, including throwing away the current implementation
4. **You have full autonomy** - Do not feel bound by existing patterns; if the architecture is wrong, say so

---

## Background: What We're Building

We're building a **node-based visual editor** for aircraft electrical systems. Users can:

- Create circuits by placing components (batteries, switches, lights, etc.)
- Connect components with wires
- **Nest blueprints** - A blueprint can contain other blueprints (hierarchical composition)
- **Drill down** - Double-click a blueprint to see its internal circuit
- **Simulate** - The circuit runs in real-time showing voltage/current flow

### Key Data Structures

```cpp
// Core data model (persistent, saved to JSON)
struct Node {
    std::string id;
    std::string name;
    std::string type_name;  // "Battery", "Switch", etc.
    NodeKind kind;          // Node, Blueprint, Bus, etc.
    Pt pos;                 // Position in world
    std::vector<Port> inputs;
    std::vector<Port> outputs;
    bool visible = true;    // <-- ADDED FOR COLLAPSING (problematic?)
};

struct Wire {
    std::string id;
    Connection start;       // {node_id, port_name}
    Connection end;         // {node_id, port_name}
    std::vector<Pt> routing_points;
};

struct Blueprint {
    std::vector<Node> nodes;
    std::vector<Wire> wires;
};

// Presentation layer (not persistent, created each frame)
class BaseVisualNode {
protected:
    Pt position_;
    Pt size_;
    std::string node_id_;
    bool visible_ = true;   // <-- COPIED FROM Node.visible
public:
    bool isVisible() const { return visible_; }
    void setVisible(bool visible) { visible_ = visible; }
    virtual void render(IDrawList* dl, const Viewport& vp, ...) = 0;
    virtual bool containsPoint(Pt world_pos) = 0;
    // ... more methods
};

// Cache for VisualNodes (performance optimization)
class VisualNodeCache {
    std::unordered_map<std::string, std::unique_ptr<BaseVisualNode>> cache_;
public:
    BaseVisualNode* getOrCreate(const Node& node, const std::vector<Wire>& wires);
    void clear();
    // ...
};
```

---

## Current Architecture: "Always-Flatten" with Visibility Flags

### Design Decision

When a user inserts a nested blueprint:

1. **Blueprint is expanded** - All internal devices are added to the parent blueprint
2. **Collapsed node is created** - A visual wrapper representing the nested blueprint
3. **Visibility flags control rendering** - Either collapsed OR internals are visible
4. **Simulation sees all nodes** - Both collapsed and internal nodes exist in the blueprint

### Example: Inserting a "lamp_pass_through" blueprint

```json
// File: blueprints/lamp_pass_through.json
{
  "devices": [
    {"name": "vin", "internal": "BlueprintInput", "ports": {"v_in": "In"}},
    {"name": "lamp", "internal": "IndicatorLight", ...},
    {"name": "vout", "internal": "BlueprintOutput", "ports": {"v_out": "Out"}}
  ],
  "wires": [
    {"from": "vin.v_out", "to": "lamp.v_in"},
    {"from": "lamp.v_out", "to": "vout.v_in"}
  ],
  "exposed_ports": {
    "vin": {"direction": "In", "type": "V"},
    "vout": {"direction": "Out", "type": "V"}
  }
}
```

After insertion into parent blueprint:

```
Parent blueprint (always-flatten):
┌─────────────────────────────────────────────────┐
│ bp.lamp_pass_through (collapsed, visible=true)  │ ← Visual wrapper
│   Ports: [vin, vout]                            │
│                                                  │
│ lamp_pass_through:vin (BlueprintInput, visible=false)  │ ← Hidden in parent
│ lamp_pass_through:lamp (IndicatorLight, visible=false) │ ← Hidden in parent
│ lamp_pass_through:vout (BlueprintOutput, visible=false)│ ← Hidden in parent
└─────────────────────────────────────────────────┘
```

When user drills in:
```
Drilled-in view:
┌─────────────────────────────────────────────────┐
│ bp.lamp_pass_through (collapsed, visible=false) │ ← Hidden
│                                                  │
│ lamp_pass_through:vin   (BlueprintInput, visible=true)  │ ← Shown
│ lamp_pass_through:lamp  (IndicatorLight, visible=true) │ ← Shown
│ lamp_pass_through:vout  (BlueprintOutput, visible=true)│ ← Shown
└─────────────────────────────────────────────────┘
```

### Code Locations

**Visibility Infrastructure:**
- `src/editor/data/node.h:46` - `bool visible = true` in Node struct
- `src/editor/visual_node.h:54` - `bool visible_` in BaseVisualNode
- `src/editor/visual_node.cpp:41` - Constructor initializes `visible_` from `Node.visible`

**Visibility Toggle (Drill-down):**
- `src/editor/app.cpp:946-1000` - `drill_into()` and `drill_out()` functions

**Rendering:**
- `src/editor/render.cpp:156-172` - Skip hidden nodes in render loop

**Hit Testing:**
- `src/editor/hittest.cpp:169-170` - Skip hidden ports in `hit_test_ports()`

---

## The Problems

### Problem 1: VisualNodeCache Doesn't Sync Visibility

**Symptom:** User can drag wires from non-visible (hidden) ports in parent view.

**Root Cause:** `VisualNodeCache::getOrCreate()` only syncs `node_content`, not `visible`:

```cpp
// src/editor/visual_node.cpp:576-587
BaseVisualNode* VisualNodeCache::getOrCreate(const Node& node, const std::vector<Wire>& wires) {
    auto it = cache_.find(node.id);
    if (it == cache_.end()) {
        auto visual = VisualNodeFactory::create(node, wires);
        cache_[node.id] = std::move(visual);
        return cache_[node.id].get();
    }
    // BUG: Only syncs content, not visibility!
    it->second->updateNodeContent(node.node_content);
    return it->second.get();
}
```

**Impact:** Cached VisualNodes have stale visibility state.

### Problem 2: Wires Are Always Rendered

**Symptom:** Wires connecting to hidden nodes are still visible.

**Root Cause:** `render_blueprint()` doesn't check node visibility before rendering wires:

```cpp
// src/editor/render.cpp:144-160
for (size_t wire_idx = 0; wire_idx < bp.wires.size(); wire_idx++) {
    const auto& w = bp.wires[wire_idx];
    // ... render wire ...
    // NO CHECK: Are start_node or end_node hidden?
}
```

**Impact:** In parent view, you see wires floating in space connected to nothing.

### Problem 3: Blueprint Nodes Showing Content

**Symptom:** Blueprint (collapsed) nodes show gauge/meter content when they shouldn't.

**Root Cause:** Unclear - possibly `VisualNodeFactory` falling back to wrong VisualNode type.

### Problem 4: Architectural Concerns

**Deeper Issues:**

1. **Data/Presentation Mixing** - `visible` flag exists in both Node (data) and BaseVisualNode (presentation)
2. **Cache Complexity** - VisualNodeCache adds complexity and sync bugs
3. **Always-Flatten Overhead** - Simulation sees both collapsed + internal nodes (is this efficient?)
4. **Persistence Question** - Should visibility be saved to JSON? (Currently yes, but should it be?)
5. **Multiple Systems to Update** - Rendering, hit testing, wire rendering, selection all need visibility checks

---

## Questions for You (The AI)

### Fundamental Questions

1. **Is the visibility-based approach correct?**
   - Should we filter by `visible` flag?
   - Or should we have separate "views" into the blueprint (e.g., `BlueprintView` class)?
   - Or should blueprints NOT be flattened at all?

2. **Should the collapsed node exist in the data model?**
   - Currently: Yes, both collapsed + internal nodes exist in Blueprint
   - Alternative: Collapsed node is presentation-only; data model has only internal nodes
   - Alternative: Collapsed node exists in parent, internal nodes in separate Blueprint object

3. **What should VisualNodeCache do?**
   - Keep it and fix sync bugs?
   - Remove it entirely (recreate VisualNodes each frame)?
   - Make it smarter (auto-invalidate on Node changes)?
   - Replace with weak references?

4. **Should simulation see collapsed nodes?**
   - Currently: No, collapsed node is visual-only wrapper
   - This means data model has "fake" nodes that simulation ignores
   - Is this clean separation or confusing inconsistency?

5. **How should wire visibility work?**
   - Filter wires if either endpoint is hidden?
   - Show wires but dim them when endpoint is hidden?
   - Separate "wire layers" per view level?

### Technical Questions

6. **Should `visible` be in Node (data) or just BaseVisualNode (presentation)?**
   - Current: Both (Node.visible → BaseVisualNode.visible_)
   - Problem: Cache doesn't sync, causing bugs
   - Alternative: Only in BaseVisualNode; computed from drill-down state

7. **Should we clear VisualNodeCache on visibility changes?**
   - Currently: Yes, in drill_into/drill_out
   - But cached VisualNodes may be retrieved elsewhere before cache clear
   - Alternative: Invalidate specific nodes, not entire cache

8. **How should selection work with visibility?**
   - Can you select hidden nodes? (Probably not)
   - What happens to selection when drilling in/out?
   - Should selection be per-view or global?

---

## Requirements

### Functional Requirements

Whatever architecture you propose, it must:

1. **Hierarchical blueprints** - Users can nest blueprints arbitrarily deep
2. **Visual collapsing** - Parent view shows collapsed node; drilled view shows internals
3. **Drill navigation** - Double-click to drill in, Escape to drill out
4. **Simulation correctness** - Voltage/current must flow correctly through nested blueprints
5. **Performance** - Handle ~1000 nodes without lag
6. **Persistence** - Save/load blueprints to JSON
7. **Hit testing** - Can only click/drag visible elements
8. **Wire routing** - Wires only visible when connected nodes are visible

### Non-Functional Requirements

- **Clear separation** of data model and presentation
- **Minimal bugs** - Avoid sync issues between data and presentation
- **Testability** - Architecture should be easy to test
- **Maintainability** - Future developers should understand the code
- **Extensibility** - Easy to add new features (e.g., multiple views, tabbed editing)

---

## What You Should Do

1. **Analyze the current architecture** - Understand what works and what doesn't
2. **Decide if the approach is fundamentally sound** - Is visibility-based filtering the right pattern?
3. **Propose a solution** - This could be:
   - Fix the current implementation (sync visibility, filter wires)
   - Refactor to a cleaner architecture (e.g., separate View objects)
   - Completely redesign (e.g., don't flatten blueprints at all)
4. **Implement your solution** - Make it production-ready
5. **Add tests** - Verify the fix works and prevent regressions

### You Are Allowed To:

- ✅ Change any code in `src/editor/`
- ✅ Modify data structures (Node, Wire, Blueprint, etc.)
- ✅ Add new classes, remove old ones
- ✅ Change the API between components
- ✅ Question fundamental assumptions (e.g., "should we flatten blueprints?")
- ✅ Propose alternative architectures even if they require significant refactoring

### You Should NOT:

- ❌ Break the simulation (voltage must still flow correctly)
- ❌ Remove hierarchical blueprint support
- ❌ Make the editor slower
- ❌ Break existing JSON files (must be backward compatible)

---

## Existing Tests

Read these tests to understand expected behavior:
- `tests/test_editor_hierarchical.cpp` - Visibility tests (lines 400-550)
- `tests/test_render.cpp` - Rendering tests
- `tests/test_hittest.cpp` - Hit testing tests

All tests should pass after your changes.

---

## Code Context

### Key Files

| File | Purpose | Lines |
|------|---------|-------|
| `src/editor/data/node.h` | Node, Wire, Blueprint structs | ~100 |
| `src/editor/visual_node.h` | BaseVisualNode, VisualNodeCache | ~250 |
| `src/editor/visual_node.cpp` | VisualNode implementations | ~650 |
| `src/editor/render.cpp` | Blueprint rendering | ~400 |
| `src/editor/hittest.cpp` | Hit testing for wires/ports | ~230 |
| `src/editor/app.cpp` | EditorApp, drill_into/drill_out | ~1100 |
| `src/editor/app.h` | EditorApp interface | ~180 |

### Git Branch

- Working on: `new-arch` branch
- Main branch for PRs: `main`
- Recent commits related to this work:
  - `d0a9118` - "fix: Skip hit testing for hidden nodes"
  - `f499432` - "feat: Drill-down navigation for nested blueprints"
  - `4099128` - "test: Visibility tests for blueprint collapsing"

---

## Summary

We have a **visibility-based filtering system** for hierarchical blueprints that:

- ✅ **Works for simulation** - Voltage flows correctly
- ✅ **Works for rendering** (mostly) - Hidden nodes don't render
- ❌ **Broken for hit testing** - Can click hidden ports
- ❌ **Broken for wires** - Wires to hidden nodes still visible
- ❌ **Architecturally suspect** - Cache sync issues, data/presentation mixing

**Your job:** Decide if this architecture is salvageable or needs redesign, then implement the fix.

---

## How to Approach This

1. **Start by understanding** - Read the code, run the editor, insert a blueprint, see the bugs
2. **Question everything** - Don't assume the current approach is correct
3. **Propose options** - Present 2-3 architectural alternatives with pros/cons
4. **Pick the best one** - Explain your reasoning
5. **Implement it** - Write clean, tested code
6. **Document your changes** - Update comments/tests so others understand

**Good luck!** This is a chance to fundamentally improve the editor architecture. Don't be afraid to make big changes if they're warranted.

---

*Created: 2025-03-07*
*Context: Continuing work on hierarchical blueprint visibility system*
*Previous work: Commits d0a9118, f499432, 4099128*
