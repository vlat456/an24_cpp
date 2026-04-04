# Port Layout Override — Implementation Plan

> **Feature**: Allow users to customize which side (Left/Right/Top/Bottom) a port
> appears on, with optional position ordering, via the Properties window.
>
> **Status**: ✅ Phase 1 Complete — tests pass
>
> **Scope**: Standard component nodes only. Bus nodes (`render_hint="bus"`) are
> excluded — they already have their own `port_edge` mechanism via
> `BusNodeWidget`.

---

## 1. Overview

Today, standard `NodeWidget` ports are hard-coded: inputs always appear on the
left, outputs on the right, paired row-by-row. This works for simple components
but becomes limiting for complex nodes (e.g., an APU with electrical, mechanical,
and hydraulic ports) where the user wants thermal ports on the bottom and
mechanical ports on top.

This feature adds a `layout_overrides` vector to `Node` that lets users
reassign individual ports to any of the four sides (Left, Right, Top, Bottom)
and optionally specify ordering within that side. The override data:

- Lives in `Node` as a first-class field (not stuffed into `params`)
- Matches ports by name (survives port reordering in type definitions)
- Uses `std::optional` for "auto" behavior (no override = use defaults)
- Serializes to/from the FlatBlueprint JSON format
- Integrates with the existing snapshot-based undo/redo system
- Drives a new four-sided layout algorithm in `NodeWidget`

---

## 2. Data Model Changes

### 2.1 New Enum: `PortLayoutSide` (in `port.h`)

We need a four-valued side enum distinct from the existing `PortSide`
(which is Input/Output/InOut — a logical direction, not a geometric side).

```cpp
// port.h — add after PortSide enum

/// Geometric side of a node where a port is rendered.
/// Distinct from PortSide which represents data direction (Input/Output/InOut).
enum class PortLayoutSide : uint8_t {
    Left,
    Right,
    Top,
    Bottom
};
```

**Why a separate enum?** `PortSide` conflates logical direction with geometry.
An output port (logically `PortSide::Output`) might be placed on the Top side
geometrically. Mixing these in a single enum would be confusing and error-prone.

### 2.2 New Struct: `PortLayoutOverride` (in `node.h`)

```cpp
// node.h — add before struct Node

/// Per-port layout override at the blueprint (instance) level.
/// Allows a user to move a port to a different side of the node
/// and optionally set its position within that side.
struct PortLayoutOverride {
    std::string port_name;                    ///< Match by name (survives reordering)
    std::optional<PortLayoutSide> side;       ///< Override side (nullopt = use default)
    std::optional<uint8_t> position;          ///< Position hint within side (nullopt = auto-append)
};
```

### 2.3 New Field on `Node` (in `node.h`)

```cpp
struct Node {
    // ... existing fields ...

    /// Per-port layout overrides. Empty = all ports use default placement.
    /// Only non-bus nodes use this; bus nodes use params["port_edge"].
    std::vector<PortLayoutOverride> layout_overrides;

    // ... rest of struct ...
};
```

**Why `std::vector` and not `std::unordered_map`?**
- Preserves insertion order (useful for serialization stability).
- Typically 0–10 entries — linear scan is faster than hash map overhead.
- Trivially copyable for undo snapshots.

**No changes to `Node` constructor needed** — `std::vector` default-constructs
to empty, which means "no overrides" = default layout behavior.

### 2.4 New Flat Struct: `FlatPortLayoutOverride` (in `flat_blueprint.h`)

```cpp
// flat_blueprint.h — add after FlatColor

struct FlatPortLayoutOverride {
    std::string port;                          // Port name
    std::optional<std::string> side;           // "left", "right", "top", "bottom"
    std::optional<int> position;               // Position hint (0-based)
};
```

### 2.5 New Field on `FlatNode` (in `flat_blueprint.h`)

```cpp
struct FlatNode {
    // ... existing fields ...
    std::vector<FlatPortLayoutOverride> layout_overrides;  // NEW
};
```

---

## 3. Serialization Changes

### 3.1 JSON Schema

Layout overrides are stored as an array on each node:

```json
{
  "nodes": {
    "apu1": {
      "type": "APU",
      "pos": [100, 200],
      "layout_overrides": [
        {"port": "rpm_out",  "side": "top",    "position": 0},
        {"port": "temp_out", "side": "bottom"},
        {"port": "v_out",    "side": "right",  "position": 0}
      ]
    }
  }
}
```

**Schema rules:**
- `layout_overrides` key is **omitted** when the array is empty (default behavior).
- `port` (string, required): Port name. Must match an existing port on the type.
- `side` (string, optional): One of `"left"`, `"right"`, `"top"`, `"bottom"`.
  Omitted means "use the default side for this port's direction".
- `position` (integer, optional): 0-based position hint within the side.
  Omitted means "auto-append after all explicitly-positioned ports".

### 3.2 Parse Changes (`flat_blueprint.cpp`)

Add a new parse helper and call it from `parse_node()`:

```cpp
static FlatPortLayoutOverride parse_port_layout_override(const json& j) {
    FlatPortLayoutOverride o;
    if (j.contains("port")) o.port = j["port"].get<std::string>();
    if (j.contains("side")) o.side = j["side"].get<std::string>();
    if (j.contains("position")) o.position = j["position"].get<int>();
    return o;
}
```

In `parse_node()`, after the existing `blueprint_path` parsing:

```cpp
if (j.contains("layout_overrides") && j["layout_overrides"].is_array()) {
    for (const auto& ov : j["layout_overrides"]) {
        n.layout_overrides.push_back(parse_port_layout_override(ov));
    }
}
```

### 3.3 Serialize Changes (`flat_blueprint.cpp`)

Add a new serialize helper and call it from `serialize_node()`:

```cpp
static json serialize_port_layout_override(const FlatPortLayoutOverride& o) {
    json j;
    j["port"] = o.port;
    if (o.side.has_value()) j["side"] = *o.side;
    if (o.position.has_value()) j["position"] = *o.position;
    return j;
}
```

In `serialize_node()`, after the existing `blueprint_path` emission:

```cpp
if (!n.layout_overrides.empty()) {
    json arr = json::array();
    for (const auto& ov : n.layout_overrides) {
        arr.push_back(serialize_port_layout_override(ov));
    }
    j["layout_overrides"] = arr;
}
```

### 3.4 Conversion Changes (`blueprint.cpp`)

**`node_to_flat()`** — convert `PortLayoutOverride` → `FlatPortLayoutOverride`:

```cpp
// In node_to_flat(), after nv.blueprint_path assignment:
for (const auto& ov : n.layout_overrides) {
    FlatPortLayoutOverride fov;
    fov.port = ov.port_name;
    if (ov.side.has_value()) {
        switch (*ov.side) {
            case PortLayoutSide::Left:   fov.side = "left";   break;
            case PortLayoutSide::Right:  fov.side = "right";  break;
            case PortLayoutSide::Top:    fov.side = "top";    break;
            case PortLayoutSide::Bottom: fov.side = "bottom"; break;
        }
    }
    if (ov.position.has_value()) {
        fov.position = static_cast<int>(*ov.position);
    }
    nv.layout_overrides.push_back(std::move(fov));
}
```

**`from_flat()`** — convert `FlatPortLayoutOverride` → `PortLayoutOverride`:

```cpp
// In from_flat(), inside the node construction loop, after color handling:
for (const auto& fov : nv.layout_overrides) {
    PortLayoutOverride ov;
    ov.port_name = fov.port;
    if (fov.side.has_value()) {
        const std::string& s = *fov.side;
        if      (s == "left")   ov.side = PortLayoutSide::Left;
        else if (s == "right")  ov.side = PortLayoutSide::Right;
        else if (s == "top")    ov.side = PortLayoutSide::Top;
        else if (s == "bottom") ov.side = PortLayoutSide::Bottom;
    }
    if (fov.position.has_value()) {
        ov.position = static_cast<uint8_t>(*fov.position);
    }
    n.layout_overrides.push_back(std::move(ov));
}
```

### 3.5 String ↔ Enum Helpers

Add utility functions to `port.h` (or a new `port_layout.h`):

```cpp
/// Convert PortLayoutSide to string for serialization.
inline const char* port_layout_side_to_string(PortLayoutSide s) {
    switch (s) {
        case PortLayoutSide::Left:   return "left";
        case PortLayoutSide::Right:  return "right";
        case PortLayoutSide::Top:    return "top";
        case PortLayoutSide::Bottom: return "bottom";
    }
    return "left";
}

/// Parse string to PortLayoutSide. Returns nullopt on unknown string.
inline std::optional<PortLayoutSide> parse_port_layout_side(const std::string& s) {
    if (s == "left")   return PortLayoutSide::Left;
    if (s == "right")  return PortLayoutSide::Right;
    if (s == "top")    return PortLayoutSide::Top;
    if (s == "bottom") return PortLayoutSide::Bottom;
    return std::nullopt;
}

/// Default geometric side for a port based on its logical direction.
inline PortLayoutSide default_layout_side(PortSide side) {
    switch (side) {
        case PortSide::Input:  return PortLayoutSide::Left;
        case PortSide::Output: return PortLayoutSide::Right;
        case PortSide::InOut:  return PortLayoutSide::Left;  // InOut defaults to left
    }
    return PortLayoutSide::Left;
}
```

---

## 4. Layout Resolution Algorithm

### 4.1 Data Structure: `ResolvedPort`

```cpp
/// Intermediate struct used during layout resolution.
/// Not persisted — computed fresh each time NodeWidget is built.
struct ResolvedPort {
    std::string_view name;        ///< Port name (from interner)
    PortType type;                ///< Port type (V, I, Bool, etc.)
    PortSide logical_side;        ///< Original logical direction
    PortLayoutSide layout_side;   ///< Final geometric side after override
    std::optional<uint8_t> position_hint;  ///< From override, if any
    uint8_t final_position;       ///< Assigned by resolution algorithm
};
```

### 4.2 Resolution Algorithm (Pseudocode)

```
resolve_port_layout(inputs, outputs, layout_overrides) → map<PortLayoutSide, vector<ResolvedPort>>:

    // Step 1: Build flat list of all ports with default sides
    all_ports = []
    for each port in inputs:
        all_ports.append(ResolvedPort{
            name=port.name, type=port.type,
            logical_side=Input, layout_side=Left
        })
    for each port in outputs:
        all_ports.append(ResolvedPort{
            name=port.name, type=port.type,
            logical_side=Output, layout_side=Right
        })

    // Step 2: Apply overrides by name match
    for each override in layout_overrides:
        port = find_by_name(all_ports, override.port_name)
        if port is not found:
            continue  // Orphaned override — silently ignore
        if override.side.has_value():
            port.layout_side = override.side.value()
        if override.position.has_value():
            port.position_hint = override.position.value()

    // Step 3: Group by final side
    groups = {Left: [], Right: [], Top: [], Bottom: []}
    for each port in all_ports:
        groups[port.layout_side].append(port)

    // Step 4: Sort each group — overridden ports first (by hint), then auto ports
    for each side in groups:
        overridden = [p for p in groups[side] if p.position_hint.has_value()]
        auto_ports = [p for p in groups[side] if not p.position_hint.has_value()]

        // stable_sort overridden by hint (preserves relative order on ties)
        stable_sort(overridden, by: position_hint)

        // Merge: place overridden ports at their hint positions, fill gaps with auto
        merged = []
        auto_idx = 0
        for i in 0..max(len(overridden) + len(auto_ports)):
            // Find overridden ports that want position i
            candidates = [p for p in overridden if p.position_hint == i]
            for c in candidates:
                c.final_position = len(merged)
                merged.append(c)
                overridden.remove(c)

            // If no overridden port claimed this slot and auto ports remain
            if candidates is empty and auto_idx < len(auto_ports):
                auto_ports[auto_idx].final_position = len(merged)
                merged.append(auto_ports[auto_idx])
                auto_idx++

        // Append remaining overridden ports (position > total count)
        for p in overridden:
            p.final_position = len(merged)
            merged.append(p)

        // Append remaining auto ports
        while auto_idx < len(auto_ports):
            auto_ports[auto_idx].final_position = len(merged)
            merged.append(auto_ports[auto_idx])
            auto_idx++

        groups[side] = merged

    // Step 5: Assign sequential final positions
    for each side in groups:
        for i, port in enumerate(groups[side]):
            port.final_position = i

    return groups
```

### 4.3 Simpler Alternative (Recommended for V1)

The merge logic above handles position collisions but is complex. For V1, use a
simpler approach:

```
// Step 4 simplified:
for each side in groups:
    // Partition into hinted and auto
    hinted  = filter(has position_hint)
    auto    = filter(no position_hint)

    // Sort hinted by hint value (stable sort for tie-breaking)
    stable_sort(hinted, by position_hint)

    // Concatenate: hinted first, then auto (preserving original order)
    groups[side] = hinted + auto

    // Assign final_position = index
    for i, port in enumerate(groups[side]):
        port.final_position = i
```

This means `position=0` comes before `position=1`, and auto-ports come after all
hinted ports. Position collisions (two ports with `position=0`) are resolved by
stable sort order (first one defined wins).

### 4.4 C++ Implementation Location

Create a new free function in a new file `src/editor/visual/node/port_layout_resolver.h`:

```cpp
#pragma once
#include "data/node.h"
#include "data/port.h"
#include <array>
#include <vector>
#include <string_view>

struct ResolvedPort {
    std::string_view name;
    PortType type;
    PortSide logical_side;
    PortLayoutSide layout_side;
    uint8_t final_position;
};

/// Four-sided resolved layout: [Left, Right, Top, Bottom]
using ResolvedLayout = std::array<std::vector<ResolvedPort>, 4>;

/// Resolve port layout from inputs, outputs, and overrides.
/// Returns ports grouped by geometric side with assigned positions.
ResolvedLayout resolve_port_layout(
    const std::vector<EditorPort>& inputs,
    const std::vector<EditorPort>& outputs,
    const std::vector<PortLayoutOverride>& overrides,
    const ui::StringInterner& interner);
```

This function is **pure** (no side effects, no state) and easily testable in
isolation.

---

## 5. Visual Node Changes

### 5.1 Overview

`NodeWidget::buildStandardLayout()` currently builds a simple two-column layout
(inputs left, outputs right, paired row-by-row). With port layout overrides,
it must support four sides.

**Strategy**: If `layout_overrides` is empty, keep the existing fast path
unchanged. If overrides are present, use the new four-sided layout path.

### 5.2 Modified `buildStandardLayout()`

```cpp
void NodeWidget::buildStandardLayout(const ::Node& data,
                                      const ui::StringInterner& interner) {
    // Fast path: no overrides — use existing paired-row layout
    if (data.layout_overrides.empty()) {
        buildStandardLayoutDefault(data, interner);
        return;
    }

    // Slow path: four-sided layout with overrides
    buildFourSidedLayout(data, interner);
}
```

### 5.3 New Method: `buildFourSidedLayout()`

The four-sided layout uses this widget tree structure:

```
Column (layout_)
  ├─ HeaderWidget (name)
  ├─ TopPortStrip (if any top ports)     ← Row of evenly-spaced ports
  ├─ Row (main body)
  │   ├─ LeftPortColumn                  ← Column of left-side ports
  │   ├─ ContentArea (flexible)          ← Gauge/Switch/Spacer
  │   └─ RightPortColumn                ← Column of right-side ports
  ├─ BottomPortStrip (if any bottom ports) ← Row of evenly-spaced ports
  └─ TypeNameWidget (type name)
```

**Top/Bottom port strips** distribute ports evenly along the horizontal axis,
similar to how `BusNodeWidget::calculatePortLocalPos()` works.

**Left/Right port columns** use the existing `buildPortInColumn()` method for
vertically stacked ports with labels.

```cpp
void NodeWidget::buildFourSidedLayout(const ::Node& data,
                                       const ui::StringInterner& interner) {
    ResolvedLayout layout = resolve_port_layout(
        data.inputs, data.outputs, data.layout_overrides, interner);

    auto& left_ports   = layout[static_cast<int>(PortLayoutSide::Left)];
    auto& right_ports  = layout[static_cast<int>(PortLayoutSide::Right)];
    auto& top_ports    = layout[static_cast<int>(PortLayoutSide::Top)];
    auto& bottom_ports = layout[static_cast<int>(PortLayoutSide::Bottom)];

    // Top port strip
    if (!top_ports.empty()) {
        buildHorizontalPortStrip(top_ports, PortLayoutSide::Top);
    }

    // Main body: [Left ports | Content | Right ports]
    auto* body_row = layout_->emplaceChild<Row>();
    body_row->setFlexible(true);

    // Left column
    auto* left_col = body_row->emplaceChild<Column>();
    for (const auto& rp : left_ports) {
        buildPortInColumn(left_col, rp.name, rp.type, /*is_left=*/true);
    }

    // Content area (same logic as current buildStandardLayout)
    buildContentArea(data);

    // Right column
    auto* right_col = body_row->emplaceChild<Column>();
    for (const auto& rp : right_ports) {
        buildPortInColumn(right_col, rp.name, rp.type, /*is_left=*/false);
    }

    // Bottom port strip
    if (!bottom_ports.empty()) {
        buildHorizontalPortStrip(bottom_ports, PortLayoutSide::Bottom);
    }
}
```

### 5.4 New Method: `buildHorizontalPortStrip()`

Inspired by `BusNodeWidget::calculatePortLocalPos()`:

```cpp
void NodeWidget::buildHorizontalPortStrip(
        const std::vector<ResolvedPort>& ports,
        PortLayoutSide side) {
    // Container with minimal height; ports are positioned in layout() post-pass
    auto* strip = layout_->emplaceChild<Container>(
        Edges{0, 2.0f, 0, 2.0f});
    strip->setMinHeight(PORT_ROW_HEIGHT);

    for (size_t i = 0; i < ports.size(); ++i) {
        auto* port_w = strip->emplaceChild<Port>(
            ports[i].name,
            ports[i].logical_side,
            ports[i].type);
        port_w->setTag(static_cast<int>(side));  // Tag for post-layout positioning
        ports_.push_back(port_w);
    }
}
```

### 5.5 Modified `layout()` — Post-Layout Port Positioning

The existing `layout()` method snaps left ports to the left edge and right ports
to the right edge. It must be extended to handle top/bottom ports:

```cpp
void NodeWidget::layout(float w, float h) {
    setSize(Pt(w, h));
    if (layout_) layout_->layout(w, h);

    Pt np = worldPos();
    for (auto* p : ports_) {
        Pt wp = p->worldPos();
        Pt lp = p->localPos();

        if (p->side() == PortSide::Input || p->side() == PortSide::Output) {
            // Check if this is a top/bottom port (tagged during buildHorizontalPortStrip)
            int tag = p->tag();
            if (tag == static_cast<int>(PortLayoutSide::Top) ||
                tag == static_cast<int>(PortLayoutSide::Bottom)) {
                // Horizontal distribution (evenly spaced)
                // Handled below
            } else {
                // Existing left/right snap logic
                float current_cx = wp.x + Port::RADIUS;
                if (p->side() == PortSide::Input) {
                    lp.x += np.x - current_cx;
                } else {
                    lp.x += (np.x + w) - current_cx;
                }
            }
        }

        // Vertical centering in parent (existing logic)
        if (p->parent()) {
            float parent_h = p->parent()->size().y;
            lp.y = (parent_h - Port::RADIUS * 2) / 2.0f;
        }

        p->setLocalPos(lp);
    }

    // Position top/bottom ports evenly
    positionHorizontalStrip(PortLayoutSide::Top, w, np);
    positionHorizontalStrip(PortLayoutSide::Bottom, w, np);
}
```

**`positionHorizontalStrip()`** distributes ports evenly along the node width,
using the same formula as `BusNodeWidget::calculatePortLocalPos()`:

```cpp
void NodeWidget::positionHorizontalStrip(PortLayoutSide side, float node_width,
                                          Pt node_world_pos) {
    // Collect ports tagged with this side
    std::vector<Port*> strip_ports;
    for (auto* p : ports_) {
        if (p->tag() == static_cast<int>(side)) {
            strip_ports.push_back(p);
        }
    }
    if (strip_ports.empty()) return;

    float step = node_width / (strip_ports.size() + 1);
    for (size_t i = 0; i < strip_ports.size(); ++i) {
        Port* p = strip_ports[i];
        float cx = step * (i + 1);  // Center of port along X
        Pt lp;
        lp.x = cx - Port::RADIUS;  // Local X relative to strip container

        if (side == PortLayoutSide::Top) {
            lp.y = -Port::RADIUS;  // Snap circle center to top edge
        } else {
            // Bottom: snap to parent container's bottom edge
            float parent_h = p->parent() ? p->parent()->size().y : 0;
            lp.y = parent_h - Port::RADIUS;
        }
        p->setLocalPos(lp);
    }
}
```

### 5.6 Wire Rendering Compatibility

Wires connect to ports via `portByName()` which searches the flat `ports_`
vector — **no changes needed**. The wire renderer uses port world positions
(from `worldPos()`), which will automatically reflect the new layout.

---

## 6. Properties Window UI

### 6.1 New Section: "Port Layout"

Added after the existing "Parameters" section, before the OK/Cancel buttons.
Only shown for non-bus, non-group, non-text, non-ref nodes (i.e., standard
`NodeWidget` nodes).

### 6.2 UI Mockup

```
+--------------------------------------------+
| Properties: apu1                           |
|--------------------------------------------|
| Name: [APU_1____________]                  |
|                                            |
| Parameters                                 |
| ------------------------------------------ |
| rpm_max  [8000___]                         |
| ...                                        |
|                                            |
| Port Layout                                |
| ------------------------------------------ |
| Port       | Side   | Pos | Action         |
| ---------- | ------ | --- | -------------- |
| v_in       | Left   | -   | [Reset]        |
| rpm_out    | Top  v | 0   | [Reset]        |
| temp_out   | Bottom | -   | [Reset]        |
| v_out      | Right  | 0   | [Reset]        |
| ------------------------------------------ |
|                                            |
| [OK]                    [Cancel]           |
+--------------------------------------------+
```

- **Port**: Read-only label showing port name.
- **Side**: Dropdown combo (Left/Right/Top/Bottom). Initially shows the default
  or overridden value.
- **Pos**: Optional integer input. `-` means auto (no position override).
- **Reset**: Button that clears the override for this port (reverts to default).

### 6.3 Implementation

#### 6.3.1 New Pending State

Add to `PropertiesWindow` private members:

```cpp
// Shadow copy of layout overrides (edited by UI)
std::vector<PortLayoutOverride> pending_layout_overrides_;
std::vector<PortLayoutOverride> snapshot_layout_overrides_;
```

#### 6.3.2 Snapshot on `open()`

```cpp
void PropertiesWindow::open(Node& node, ...) {
    // ... existing code ...
    snapshot_layout_overrides_ = node.layout_overrides;
    pending_layout_overrides_ = node.layout_overrides;
}
```

#### 6.3.3 Render Method: `renderPortLayoutSection()`

```cpp
void PropertiesWindow::renderPortLayoutSection(const Node& node) {
#ifndef EDITOR_TESTING
    // Skip for bus/ref/group/text nodes
    if (node.render_hint == "bus" || node.render_hint == "ref" ||
        node.render_hint == "group" || node.render_hint == "text") {
        return;
    }

    ImGui::Separator();
    ImGui::Text("Port Layout");
    ImGui::Separator();

    if (ImGui::BeginTable("##port_layout", 4,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Port", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Side", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Pos",  ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("##act",ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableHeadersRow();

        // Build combined port list (inputs + outputs)
        auto render_port = [&](const EditorPort& ep, PortSide default_side) {
            std::string port_name(bp_->interner().resolve(ep.name));
            ImGui::PushID(port_name.c_str());
            ImGui::TableNextRow();

            // Find existing override
            auto* ov = findPendingOverride(port_name);
            PortLayoutSide current_side = ov && ov->side.has_value()
                ? *ov->side
                : default_layout_side(default_side);
            bool has_override = (ov != nullptr);

            // Col 0: Port name
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(port_name.c_str());

            // Col 1: Side dropdown
            ImGui::TableNextColumn();
            const char* side_labels[] = {"Left", "Right", "Top", "Bottom"};
            int side_idx = static_cast<int>(current_side);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##side", &side_idx, side_labels, 4)) {
                ensureOverride(port_name).side =
                    static_cast<PortLayoutSide>(side_idx);
            }

            // Col 2: Position
            ImGui::TableNextColumn();
            int pos = (ov && ov->position.has_value()) ? *ov->position : -1;
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputInt("##pos", &pos, 0, 0)) {
                if (pos < 0) {
                    if (ov) ov->position = std::nullopt;
                } else {
                    ensureOverride(port_name).position =
                        static_cast<uint8_t>(pos);
                }
            }

            // Col 3: Reset button
            ImGui::TableNextColumn();
            if (has_override && ImGui::SmallButton("Reset")) {
                removePendingOverride(port_name);
            }

            ImGui::PopID();
        };

        for (const auto& p : node.inputs) {
            render_port(p, PortSide::Input);
        }
        for (const auto& p : node.outputs) {
            render_port(p, PortSide::Output);
        }

        ImGui::EndTable();
    }
#endif
}
```

#### 6.3.4 Helper Methods

```cpp
PortLayoutOverride* PropertiesWindow::findPendingOverride(
        const std::string& port_name) {
    for (auto& ov : pending_layout_overrides_) {
        if (ov.port_name == port_name) return &ov;
    }
    return nullptr;
}

PortLayoutOverride& PropertiesWindow::ensureOverride(
        const std::string& port_name) {
    if (auto* existing = findPendingOverride(port_name)) return *existing;
    pending_layout_overrides_.push_back({port_name, std::nullopt, std::nullopt});
    return pending_layout_overrides_.back();
}

void PropertiesWindow::removePendingOverride(const std::string& port_name) {
    pending_layout_overrides_.erase(
        std::remove_if(pending_layout_overrides_.begin(),
                       pending_layout_overrides_.end(),
                       [&](const PortLayoutOverride& o) {
                           return o.port_name == port_name;
                       }),
        pending_layout_overrides_.end());
}
```

#### 6.3.5 Call Site

In `PropertiesWindow::render()`, add after the param loop (before the OK/Cancel
buttons):

```cpp
renderPortLayoutSection(*target);
```

---

## 7. Undo/Redo Integration

### 7.1 Approach: Use Existing Snapshot-Based Undo

The undo system is **snapshot-based** (`UndoStack` stores full `Blueprint`
copies). Since `layout_overrides` is a field on `Node`, and `Node` is part of
`Blueprint`, it is **automatically captured** by `undo_stack.snapshot(bp)`.

No new command type is needed. The `PropertiesWindow::apply()` method already:
1. Calls `undo_stack_->snapshot(*bp_)` before any mutation
2. Applies changes via commands
3. The snapshot restores the entire `Blueprint` on undo

### 7.2 New Command: `CmdSetPortLayout`

Although snapshot-based undo handles restoration, we still need a command to
**apply** the layout override change to the live Blueprint:

```cpp
// commands.h

struct CmdSetPortLayout {
    ui::InternedId node_id;
    std::vector<PortLayoutOverride> new_overrides;
};
```

Add to the `Command` variant:

```cpp
using Command = std::variant<
    // ... existing ...
    CmdSetPortLayout
>;
```

Implementation in `commands.cpp`:

```cpp
static void execute(Blueprint& bp, const CmdSetPortLayout& cmd) {
    auto it = bp.node_index_.find(cmd.node_id);
    if (it == bp.node_index_.end()) {
        spdlog::warn("[cmd] CmdSetPortLayout: node {} not found", cmd.node_id.raw());
        return;
    }
    bp.nodes[it->second].layout_overrides = cmd.new_overrides;
}
```

Factory:

```cpp
inline Command cmd_set_port_layout(ui::InternedId id,
                                    std::vector<PortLayoutOverride> overrides) {
    return CmdSetPortLayout{id, std::move(overrides)};
}
```

### 7.3 Modified `PropertiesWindow::apply()`

Add layout override diffing after the existing name/param diffing:

```cpp
// In apply(), after the name change check:
bool layout_changed = (pending_layout_overrides_ != snapshot_layout_overrides_);

// Update has_changes
if (layout_changed) has_changes = true;

// In the "if (has_changes)" block, after name change application:
if (layout_changed) {
    execute(*bp_, cmd_set_port_layout(node_iid, pending_layout_overrides_));
}
```

Note: This requires adding `operator==` to `PortLayoutOverride`:

```cpp
bool operator==(const PortLayoutOverride& other) const {
    return port_name == other.port_name &&
           side == other.side &&
           position == other.position;
}
```

---

## 8. Edge Cases

### 8.1 Orphaned Override (Port Name Not Found)

**Scenario**: User saves a layout override for port "temp_out", then the type
definition is updated to remove that port.

**Behavior**: The resolution algorithm silently skips overrides that don't match
any port name. The orphaned override is preserved in the data (no data loss) but
has no visual effect. The Properties window will not show it (it only iterates
the actual ports from the type definition).

**Cleanup**: Optional — a "clean orphaned overrides" utility could be added
later, but it's not blocking for V1.

### 8.2 Position Collisions (Two Ports with position=0)

**Scenario**: Two ports both have `position=0` on the right side.

**Behavior**: `stable_sort` by position hint preserves the order they appear in
the overrides vector. First defined wins the lower position. This is
deterministic and predictable.

### 8.3 Partial Overrides (Side Set, Position Not Set)

**Scenario**: User sets `side=Top` but leaves `position=nullopt`.

**Behavior**: The port is placed on the Top side. Since its position is auto,
it comes after all ports on Top that have explicit position hints. If it's the
only port on Top, it gets position 0. This is the most common expected use case.

### 8.4 InOut Ports (Bidirectional)

**Scenario**: A port with `PortSide::InOut` (e.g., on a bus-like component that
isn't a Bus node).

**Behavior**: During type definition loading (`from_flat()`), InOut ports
create entries in **both** `inputs` and `outputs` vectors. The layout resolver
sees both copies. If the user overrides "v" to Top, both the input copy and
output copy will be matched and moved to Top. This is correct — from the
layout perspective, it's a single physical port that happens to be bidirectional.

**Alternative (if problematic)**: Deduplicate by port name in the resolver — if
a port name appears in both inputs and outputs, treat it as a single port for
layout purposes. Flag it as `InOut` so the visual layer knows to draw it with
the InOut style.

### 8.5 Dynamic Port Counts

**Scenario**: A component type is updated to add a new port after overrides
were saved.

**Behavior**: The new port has no override, so it appears in its default
position (inputs → Left, outputs → Right). Existing overrides continue to work
for their named ports. The layout naturally accommodates the new port.

### 8.6 Empty Override Cleanup

**Scenario**: User clicks "Reset" on all ports — `pending_layout_overrides_`
becomes empty.

**Behavior**: `CmdSetPortLayout` sets `layout_overrides = {}`. On the next
`buildStandardLayout()` call, the fast path (no overrides) is taken. JSON
serialization omits the `layout_overrides` key entirely.

### 8.7 Node Recreation After Override Change

**Scenario**: After applying overrides, the visual node needs to reflect the
new layout.

**Behavior**: The `on_apply` callback (passed to `PropertiesWindow::open()`)
already triggers node recreation in the editor. The app's callback calls
`recreate_node_widget()` which destroys the old `NodeWidget` and creates a new
one from the updated `Node` data. The new `NodeWidget` constructor reads
`layout_overrides` and builds the four-sided layout.

### 8.8 Copy/Paste of Nodes

**Scenario**: User copies a node with layout overrides and pastes it.

**Behavior**: `layout_overrides` is a value member of `Node`, so it is
automatically deep-copied. The pasted node inherits the same overrides.

---

## 9. Testing Strategy

### 9.1 Unit Tests: Resolution Algorithm

File: `tests/test_port_layout_resolver.cpp`

```cpp
TEST(PortLayoutResolver, DefaultLayout_NoOverrides) {
    // Given: 2 inputs, 2 outputs, no overrides
    // Expect: inputs on Left, outputs on Right
}

TEST(PortLayoutResolver, OverrideSide_MoveInputToRight) {
    // Given: input "v_in" with override side=Right
    // Expect: "v_in" appears in Right group
}

TEST(PortLayoutResolver, OverrideSide_MoveOutputToTop) {
    // Given: output "rpm_out" with override side=Top
    // Expect: "rpm_out" appears in Top group
}

TEST(PortLayoutResolver, OverridePosition_Ordering) {
    // Given: two outputs on Right, one with position=1, one with position=0
    // Expect: position=0 first, position=1 second
}

TEST(PortLayoutResolver, MixedOverrideAndAuto) {
    // Given: 3 ports on Left, one overridden to position=0
    // Expect: overridden port first, then auto ports in original order
}

TEST(PortLayoutResolver, OrphanedOverride_Ignored) {
    // Given: override for "nonexistent_port"
    // Expect: override silently ignored, all real ports use defaults
}

TEST(PortLayoutResolver, PositionCollision_StableSortOrder) {
    // Given: two ports both with position=0 on Right
    // Expect: first-defined port gets index 0, second gets index 1
}

TEST(PortLayoutResolver, PartialOverride_SideOnly) {
    // Given: override with side=Bottom, no position
    // Expect: port on Bottom, auto-positioned (index 0 if alone)
}

TEST(PortLayoutResolver, AllPortsMovedToOneSide) {
    // Given: all 4 ports overridden to Top
    // Expect: Top has 4 ports, other sides empty
}

TEST(PortLayoutResolver, EmptyOverrides_EqualsDefault) {
    // Given: empty overrides vector
    // Expect: identical to no-override behavior
}
```

### 9.2 Unit Tests: Serialization Roundtrip

File: `tests/test_blueprint_v2.cpp` (extend existing)

```cpp
TEST(BlueprintV2, LayoutOverrides_SerializeRoundtrip) {
    // Create a FlatBlueprint with layout_overrides on a node
    // Serialize to JSON string
    // Parse back
    // Verify overrides match
}

TEST(BlueprintV2, LayoutOverrides_OmittedWhenEmpty) {
    // Create a FlatNode with no layout_overrides
    // Serialize
    // Verify JSON does not contain "layout_overrides" key
}

TEST(BlueprintV2, LayoutOverrides_PartialOverride) {
    // Override with side only (no position)
    // Roundtrip
    // Verify position is nullopt after parse
}
```

### 9.3 Unit Tests: Command

File: `tests/test_commands.cpp` (extend existing)

```cpp
TEST_F(CommandTest, SetPortLayout_MutatesNode) {
    // Add a node, execute CmdSetPortLayout, verify layout_overrides changed
}

TEST_F(CommandTest, SetPortLayout_UndoRestores) {
    // Snapshot, execute, undo, verify overrides are restored to original
}

TEST_F(CommandTest, SetPortLayout_NodeNotFound) {
    // Execute with bad node ID, verify no crash (logged warning)
}
```

### 9.4 Unit Tests: PropertiesWindow Integration

File: `tests/test_properties_window.cpp` (extend existing)

```cpp
TEST(PropertiesWindow, LayoutOverride_ApplyEmitsCommand) {
    // Open window, set pending layout override, apply
    // Verify node.layout_overrides updated
}

TEST(PropertiesWindow, LayoutOverride_CancelDiscards) {
    // Open, modify pending overrides, cancel
    // Verify node.layout_overrides unchanged
}

TEST(PropertiesWindow, LayoutOverride_UndoReverts) {
    // Open, apply override, undo
    // Verify node.layout_overrides reverted
}

TEST(PropertiesWindow, LayoutOverride_NoChangeNoPush) {
    // Open, don't modify overrides, apply
    // Verify no undo entry pushed
}
```

### 9.5 Visual Tests (Manual)

Since `NodeWidget` rendering requires a GPU context, visual correctness is
tested manually:

1. Open editor, place a Battery node
2. Open Properties, move `v_out` to Top
3. Verify port circle appears centered on top edge
4. Verify wire connects correctly to the moved port
5. Undo — port returns to Right
6. Save, reload — port still on Top
7. Resize node — top port stays centered

---

## 10. Implementation Order

### Phase 1: Data Model + Serialization (no visual changes)

**Files**: `port.h`, `node.h`, `flat_blueprint.h`, `flat_blueprint.cpp`,
`blueprint.cpp`

**Tasks**:
1. Add `PortLayoutSide` enum to `port.h`
2. Add `PortLayoutOverride` struct to `node.h`
3. Add `layout_overrides` field to `Node`
4. Add `FlatPortLayoutOverride` to `flat_blueprint.h`
5. Add `layout_overrides` to `FlatNode`
6. Implement parse/serialize in `flat_blueprint.cpp`
7. Implement conversion in `blueprint.cpp` (`node_to_flat`, `from_flat`)
8. Add string ↔ enum helpers
9. Write serialization roundtrip tests

**Validation**: All existing tests pass. New serialization tests pass.
Saving/loading a blueprint with manually-added `layout_overrides` in JSON works.

### Phase 2: Resolution Algorithm (testable in isolation)

**Files**: New `src/editor/visual/node/port_layout_resolver.h` (header-only or
with `.cpp`), new `tests/test_port_layout_resolver.cpp`

**Tasks**:
1. Implement `resolve_port_layout()` function
2. Write comprehensive unit tests (see 9.1)

**Validation**: Pure logic tests pass. No editor/visual dependencies.

### Phase 3: Command

**Files**: `commands.h`, `commands.cpp`

**Tasks**:
1. Add `CmdSetPortLayout` struct
2. Add to `Command` variant
3. Implement `execute()` overload
4. Add factory function
5. Write command tests

**Validation**: Command tests pass. Undo/redo roundtrip works.

### Phase 4: Visual Node — Four-Sided Layout

**Files**: `visual_node.h`, `visual_node.cpp`

**Tasks**:
1. Add `buildFourSidedLayout()` method
2. Add `buildHorizontalPortStrip()` method
3. Add `positionHorizontalStrip()` method
4. Modify `buildStandardLayout()` to branch on overrides
5. Extend `layout()` for top/bottom port positioning
6. Manual visual testing

**Validation**: Editor renders correctly with overridden ports. Wire connections
work. Existing nodes without overrides are unchanged.

### Phase 5: Properties Window UI

**Files**: `properties_window.h`, `properties_window.cpp`

**Tasks**:
1. Add pending/snapshot layout override members
2. Implement `renderPortLayoutSection()`
3. Implement helper methods (`findPendingOverride`, `ensureOverride`, `removePendingOverride`)
4. Modify `open()` to snapshot layout overrides
5. Modify `apply()` to diff and emit `CmdSetPortLayout`
6. Write PropertiesWindow integration tests

**Validation**: Full end-to-end flow works: open properties → change port side →
OK → node updates → undo reverts → save/load preserves.

### Phase 6: Polish + Edge Cases

**Tasks**:
1. Handle InOut port deduplication if needed
2. Test with dynamic port counts (components with variable ports)
3. Test copy/paste of nodes with overrides
4. Test sub-blueprint nodes (overrides should NOT apply to internal nodes of
   collapsed groups — only to the group's exposed ports)
5. Add tooltip in Properties window explaining the feature
6. Verify no performance regression (benchmark with 100+ nodes)

---

## Appendix A: Files Modified (Summary)

| File | Change |
|------|--------|
| `src/editor/data/port.h` | Add `PortLayoutSide` enum, helper functions |
| `src/editor/data/node.h` | Add `PortLayoutOverride` struct, `layout_overrides` field on `Node` |
| `src/editor/data/flat_blueprint.h` | Add `FlatPortLayoutOverride`, field on `FlatNode` |
| `src/editor/data/flat_blueprint.cpp` | Parse/serialize `layout_overrides` |
| `src/editor/data/blueprint.cpp` | `node_to_flat` / `from_flat` conversion |
| `src/editor/visual/node/port_layout_resolver.h` | **NEW** — resolution algorithm |
| `src/editor/visual/node/visual_node.h` | New method declarations |
| `src/editor/visual/node/visual_node.cpp` | Four-sided layout, horizontal strip, positioning |
| `src/editor/window/properties_window.h` | Pending/snapshot layout state, new methods |
| `src/editor/window/properties_window.cpp` | Port layout UI section, apply integration |
| `src/editor/commands/commands.h` | `CmdSetPortLayout` struct + factory |
| `src/editor/commands/commands.cpp` | Execute implementation |
| `tests/test_port_layout_resolver.cpp` | **NEW** — resolution algorithm tests |
| `tests/test_blueprint_v2.cpp` | Extended — serialization roundtrip tests |
| `tests/test_commands.cpp` | Extended — command tests |
| `tests/test_properties_window.cpp` | Extended — UI integration tests |

## Appendix B: Estimated Effort

| Phase | Estimated Time |
|-------|---------------|
| Phase 1: Data Model + Serialization | 2–3 hours |
| Phase 2: Resolution Algorithm | 2–3 hours |
| Phase 3: Command | 30 minutes |
| Phase 4: Visual Node Layout | 4–6 hours |
| Phase 5: Properties Window UI | 2–3 hours |
| Phase 6: Polish + Edge Cases | 2–3 hours |
| **Total** | **~13–18 hours** |

Phase 4 is the most complex due to the new four-sided widget tree layout and
the post-layout positioning math. Consider doing it incrementally:
- 4a: Left/Right only (same as today but driven by resolver)
- 4b: Add Top/Bottom strips
- 4c: Mixed configurations

## Appendix C: What We're NOT Doing (Explicit Non-Goals)

- **Per-port drag-and-drop reordering** in the canvas (future feature)
- **Visual preview** in the Properties window (too complex for V1)
- **Bus node integration** (buses use `port_edge` param, which is a different mechanism)
- **Port label rotation** for top/bottom ports (labels remain horizontal; may
  need truncation for narrow nodes)
- **Arbitrary XY port positioning** (only side + order, not pixel-level placement)
