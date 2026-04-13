# Visual Editor

## Architecture Overview

```
┌───────────────────────────────────────────────────────────────────────┐
│ WindowSystem (manages all documents + global panels)                 │
├───────────────────────────────────────────────────────────────────────┤
│ Document (one per tab/file)                                          │
│ ├── EditorModel (bp2::EditorModel) — blueprint data                  │
│ ├── StringInterner + PathArena — string interning                    │
│ ├── Simulator<JIT_Solver> — runtime simulation                      │
│ └── WindowManager                                                     │
│     └── BlueprintWindow[] — MDI windows                              │
│         ├── Scene (widget tree)                                      │
│         │   ├── NodeWidget (component nodes)                          │
│         │   │   ├── Ports                                             │
│         │   │   └── Content widgets (gauges, switches)               │
│         │   └── Wire paths                                           │
│         ├── Viewport (pan/zoom/grid)                                 │
│         └── CanvasInput (mouse/keyboard FSM)                         │
├───────────────────────────────────────────────────────────────────────┤
│ Global Panels: Inspector, PropertiesWindow, Oscilloscope           │
└───────────────────────────────────────────────────────────────────────┘
```

## Key Classes

### WindowSystem
Top-level controller managing documents and global UI:
```cpp
class WindowSystem {
    std::vector<std::unique_ptr<Document>> documents_;
    Document* active_document_ = nullptr;
    Inspector inspector_;
    PropertiesWindow properties_window_;
    TypeRegistry type_registry_;
    OscilloscopeModel oscilloscope_;

public:
    Document& createDocument();
    Document* openDocument(const std::string& path);
    void setActiveDocument(Document* doc);
    Inspector& inspector();
    PropertiesWindow& propertiesWindow();
    TypeRegistry& typeRegistry();
};
```

### Document
Single open document - owns blueprint, simulation, and windows:
```cpp
class Document {
    std::string id_;
    std::string filepath_;

    ui::StringInterner interner_;
    bp2::PathArena arena_{interner_};
    bp2::EditorModel model_;
    WindowManager window_manager_;
    Simulator<JIT_Solver> simulation_;

    std::unordered_map<std::string, float> signal_overrides_;
    std::unordered_set<std::string> held_buttons_;

public:
    bp2::Blueprint const& blueprint() const { return model_.current(); }
    bp2::EditorModel& model() { return model_; }
    Simulator<JIT_Solver>& simulation() { return simulation_; }

    void startSimulation();
    void stopSimulation();
    void rebuildSimulation();
    void updateSimulationStep(double dt);

    void triggerSwitch(const std::string& node_id);
    void setSliderValue(const std::string& node_id, float value);
    void holdButtonPress(const std::string& node_id);
    void holdButtonRelease(const std::string& node_id);

    void addComponent(const std::string& classname, Pt world_pos,
                      const std::string& group_id, TypeRegistry& registry);
    void addBlueprint(const std::string& blueprint_name, Pt world_pos,
                      const std::string& group_id, TypeRegistry& registry);
};
```

Implementation is now split by concern:

- `src/editor/document.cpp` — constructor/title + extraction glue
- `src/editor/document_components.cpp` — component/blueprint authoring
- `src/editor/document_export.cpp` — simulation JSON export helpers
- `src/editor/document_input.cpp` — input result dispatch
- `src/editor/document_history.cpp` — undo/redo orchestration
- `src/editor/document_io.cpp` — save/load and wire-id sync
- `src/editor/document_simulation.cpp` — simulation lifecycle, overrides, node-content updates, energized-wire readout
- `src/editor/document_windows.cpp` — sub-window and external-reference window open logic

### WindowManager
MDI window management - each sub-window or nested blueprint is a separate window:
```cpp
class WindowManager {
    bp2::EditorModel& model_;
    ui::StringInterner& interner_;
    bp2::PathArena& arena_;
    std::vector<std::unique_ptr<BlueprintWindow>> windows_;

public:
    BlueprintWindow& root();                    // Main canvas
    std::pair<BlueprintWindow*, bool> open(const std::string& group_id, const std::string& title);
    void close(const std::string& group_id);
    BlueprintWindow* find(const std::string& group_id);
    BlueprintWindow* find_external(const std::string& parent_instance_id);
};
```

### BlueprintWindow
Single canvas view with scene, viewport, and input:
```cpp
class BlueprintWindow {
    bp2::EditorModel& model_;
    ui::StringInterner& interner_;
    bp2::PathArena& arena_;
    std::string group_id_;          // Empty = root window
    std::string title_;

    visual::Scene scene_;
    Viewport viewport_;
    CanvasInput input_;

public:
    void render();
    void rebuild();                  // Rebuild scene from blueprint
};
```

### Scene
Widget tree with Z-order rendering:
```cpp
class Scene : public ui::Scene {
public:
    // Insert widget sorted by RenderLayer
    ui::Widget* add(std::unique_ptr<ui::Widget> w) override;

    // Type-safe find
    Widget* find(std::string_view id) const;

    // Render with domain-specific context
    void render(IDrawList* dl, const RenderContext& ctx);
};
```

### Viewport
Pan/zoom transform with grid snapping:
```cpp
struct Viewport {
    Pt pan;            // World coordinates at screen origin
    float zoom;        // 1.0 = 100%
    float grid_step;   // Grid snapping (default 16.0f)

    Pt screen_to_world(Pt screen, Pt canvas_min) const;
    Pt world_to_screen(Pt world, Pt canvas_min) const;
    void pan_by(Pt screen_delta);
    void zoom_at(float delta, Pt screen_pos, Pt canvas_min);
    void fit_content(Pt content_min, Pt content_max, float window_w, float window_h);
};
```

## Widget Hierarchy

### visual::Widget
Base visual widget with Z-ordering:
```cpp
class Widget : public ui::Widget {
    RenderLayer layer_;
    Scene* scene_;

public:
    virtual RenderLayer renderLayer() const { return RenderLayer::Normal; }
    virtual Port* portByName(std::string_view port_name, std::string_view wire_id = {}) const;
    virtual void updateFromContent(const NodeContent& content);
    virtual void setCustomColor(std::optional<uint32_t> c);
    virtual std::optional<uint32_t> customColor() const;

    void render(IDrawList* dl, const RenderContext& ctx) const;
    void renderTree(IDrawList* dl, const RenderContext& ctx) const;
};

enum class RenderLayer : uint8_t {
    Group  = 0,   // Behind everything (group containers)
    Text   = 1,   // Behind nodes (text annotations)
    Normal = 2,   // Component nodes
    Wire   = 3    // Wires (topmost)
};
```

### NodeWidget
Component node rendering:
```cpp
class NodeWidget : public visual::Widget {
    ui::InternedId node_iid_;
    std::string name_;
    std::string type_name_;

    Column* layout_ = nullptr;
    Widget* content_widget_ = nullptr;
    std::vector<Port*> ports_;
    LayoutContext layout_ctx_;

public:
    explicit NodeWidget(const bp2::Blueprint::Node& data, const ui::StringInterner& interner);

    std::string_view id() const override;
    std::string_view nodeId() const;
    const std::string& name() const;
    const std::string& typeName() const;

    void updateContent(const NodeContent& content);
    Port* port(std::string_view name) const;
    Port* portByName(std::string_view port_name, std::string_view wire_id = {}) const override;

    void layout(float w, float h) override;
    void render(IDrawList* dl, const RenderContext& ctx) const override;
    void renderPost(IDrawList* dl, const RenderContext& ctx) const override;

    Bounds contentBounds() const;
};
```

### Port
Connection point widget:
```cpp
class Port : public visual::Widget {
    ui::InternedId node_iid_;
    ui::InternedId port_name_;
    Domain domain_;
    PortDirection direction_;
    PortType type_;

public:
    Pt connectionPoint() const;
    Domain domain() const;
    PortDirection direction() const;
    PortType type() const;
};
```

## Input Handling

### CanvasInput
Unified input handler with FSM state machine:
```cpp
class CanvasInput {
    visual::Scene& scene_;
    Viewport& viewport_;
    bp2::EditorModel& model_;
    ui::StringInterner& interner_;

    InputState state_ = InputState::Idle;
    std::vector<ui::InternedId> selected_node_ids_;
    ui::InternedId selected_wire_id_;
    ui::InternedId hovered_wire_id_;

    // Drag state
    Pt drag_anchor_;
    std::vector<Pt> drag_offsets_;

    // Wire creation
    visual::Port* wire_start_port_ = nullptr;

public:
    bool read_only = false;  // Suppress editing gestures

    InputResult on_mouse_down(Pt screen_pos, MouseButton btn, Pt canvas_min, Modifiers mods = {});
    InputResult on_mouse_up(MouseButton btn, Pt screen_pos, Pt canvas_min);
    InputResult on_mouse_drag(MouseButton btn, Pt screen_delta, Pt canvas_min);
    InputResult on_scroll(float delta, Pt screen_pos, Pt canvas_min);
    InputResult on_double_click(Pt screen_pos, Pt canvas_min);
    InputResult on_key(Key key);

    const std::vector<ui::InternedId>& selected_node_ids() const;
    std::vector<visual::Widget*> selected_nodes() const;
    visual::Wire* selected_wire() const;
    visual::Wire* hovered_wire() const;

    void clear_selection();
    void add_node_selection(visual::Widget* w);
    void snapshot_and_execute(Command cmd);
    void cancel_gesture();
};
```

### InputState FSM
States: Idle, Panning, DraggingNode, DraggingRoutingPoint, CreatingWire, ReconnectingWire, ResizingNode, MarqueeSelect, DraggingSlider

### RenderContext
Bundles all render frame state:
```cpp
struct RenderContext : public ui::RenderContext {
    const std::vector<Widget*>* selected_nodes = nullptr;
    const Wire* selected_wire = nullptr;
    const Wire* hovered_wire = nullptr;
    const RoutingPoint* hovered_routing_point = nullptr;
    const std::unordered_set<std::string_view, StringViewHash>* energized_wires = nullptr;  // nullptr when sim off

    bool isNodeSelected(const Widget* w) const;
};
```

## Signal Overrides (Interactive Control)

Documents maintain override maps for interactive simulation control:
```cpp
// Switch toggle - click toggles state
void Document::triggerSwitch(const std::string& node_id);

// Slider drag - sets value directly
void Document::setSliderValue(const std::string& node_id, float value);

// Hold button - press and release
void Document::holdButtonPress(const std::string& node_id);
void Document::holdButtonRelease(const std::string& node_id);
```

These map to `signal_overrides_` in the simulation layer.

## Sub-Windows and External References

### Nested Blueprints
Each nested blueprint gets its own window:
```cpp
void Document::openSubWindow(const std::string& sub_blueprint_id);
```

### External Reference Windows
Parent-bound read-only windows for composite nodes:
```cpp
void Document::openExternalRefWindow(const std::string& instance_id,
                                     const std::string& blueprint_file_path);
```

## Key Files

| Purpose | File |
|---------|------|
| WindowSystem | `src/editor/window_system.h` |
| Document | `src/editor/document.h` |
| WindowManager | `src/editor/window/window_manager.h` |
| BlueprintWindow | `src/editor/window/blueprint_window.h` |
| Scene | `src/editor/visual/scene.h` |
| Viewport | `src/editor/viewport/viewport.h` |
| CanvasInput | `src/editor/input/canvas_input.h` |
| NodeWidget | `src/editor/visual/node/visual_node.h` |
| RefNodeWidget | `src/editor/visual/node/ref_node_widget.h` |
| Port | `src/editor/visual/port/visual_port.h` |
| RenderContext | `src/editor/visual/render_context.h` |
| EditorApp | `src/editor/app/editor_app.h` |

## Ref/Value Nodes (render_hint: "ref")

Ref/Value nodes are special lightweight nodes that display a single value (numeric constant) inline without a full component header.

### Behavior

- **Non-resizable** — inherits `Widget::isResizable() == false`, no resize handles drawn
- **Dynamic size** — width = text content width + 16px horizontal padding; height = font height + 4px vertical padding
- **No grid snapping on size** — size is computed from content, not snapped to `PORT_LAYOUT_GRID`
- **Auto-facing port** — the single port automatically faces toward the connected node:
  - Connected node to the right → port on right edge
  - Connected node to the left → port on left edge
  - Connected node below → port on bottom edge
  - Connected node above → port on top edge
- **Half-grid dragging** — when only Ref/Value nodes are selected, drag snaps to half-grid (between grid lines) instead of full grid intersections
- **Vertical centering** — value text is rendered directly with proper vertical centering in the node body

### Port Auto-Orientation Triggers

1. **At scene rebuild** — `orient_ref_node_ports()` in `scene_mutations.cpp` runs after all node widgets are created
2. **At mouse-release drag commit** — `orient_ref_node_port()` in `canvas_input.cpp` re-orients moved nodes and their connected ref/value nodes

### Files

| File | Role |
|------|------|
| `ref_node_widget.h` | `setPortLayoutSide()`, `port_layout_side_` member |
| `ref_node_widget.cpp` | Dynamic size from text, direct text rendering with vertical centering |
| `scene_mutations.cpp` | `orient_ref_node_ports()` — rebuild-time orientation |
| `canvas_input.cpp` | `orient_ref_node_port()` — drag-commit orientation |
| `snap.h` | `snap_to_half_grid()` — snaps to half-grid (grid/2 increments) |

## Wire Rendering

### Simple Wires (2-point, no routing points)

Simple wires are rendered as a single cubic Bezier curve for a cleaner visual appearance:
- Control points are horizontal handles: `c1 = start + (handle, 0)`, `c2 = end - (handle, 0)`
- `handle = clamp(|dx| * 0.45, 20, 140)` — adapts curve tightness to wire length
- Arrowhead follows Bezier tangent at end (`end - c2`)

### Routed Wires (with intermediate routing points)

Rendered as polyline segments (unchanged), with crossing arc gaps preserved.

### Hit Testing

Hit testing and crossing detection use the underlying polyline geometry, not the Bezier samples. This means for 2-point Bezier wires, click detection uses the straight-line approximation.

## Grid Snapping

| Action | Default | Shift held |
|--------|---------|------------|
| Node drag (standard) | Grid intersections | Grid intersections |
| Node drag (ref/value only) | Half-grid (between lines) | Grid intersections |
| Routing point drag | Free (no snap) | Grid intersections |
| Resize drag | Grid snap | Grid snap |

## Port Layout

### Multi-Port Nodes

Port groups (all ports on one edge) are **centered on the respective node edge**:
- Left/right side columns have Spacer children above and below port rows
- Per-port spacing preserved (each port still gets `ROW_HEIGHT` vertical space)
- Top/bottom port strips use mathematically centered positions without layout-grid snapping on X

### Single-Port Nodes (Ref/Value)

Port is placed at the **exact geometric center** of the selected edge:
- No snapping to `PORT_LAYOUT_GRID` on center position
- Only clamped to keep port circle within node body bounds

## Composite Blueprint Insertion

When a user right-clicks → "Insert Blueprint" in the editor, the composite is inserted via `Document::addBlueprint()`.

### How It Works (Single-Source Model — Issue #23)

1. **Load from `.blueprint` file** — uses `load_blueprint_from_file_validated()` with library path lookup via `registry.categories[blueprint_name]` (e.g., `"systems"` → `library/systems/12SAM28.blueprint`)

2. **Create collapsed node + nested record** — only TWO objects are created:
   - A **collapsed visual node** at root level (expandable, with interface ports matching the blueprint's ports)
   - A **`bp2::Blueprint::Nested` record** with `embedded = true` and `inline_def` containing the loaded blueprint
   
   Internal nodes and wires live **exclusively** inside `nested.inline_def`. No shadow copies are promoted to the root blueprint.

3. **Bridge nodes** (BlueprintInput/BlueprintOutput) use **colon convention** for IDs: `unique_id:original_name` (e.g., `"GroundPower_1:v_in"`). This ensures `build_simulation_json()` and `signal_key_resolver` can reliably identify bridge nodes.

4. **Interface edits** — `add_bridge_port_to_composite()` builds a single `PortDescriptor` and applies it to both the collapsed node's visual/semantic interface and the nested record's interface in one mutation path.

5. **Sub-window viewport** — `openSubWindow()` initializes viewport to default (pan=0, zoom=1, auto-fit content). Viewport state is not persisted in canonical blueprints (post-#86).

### Sub-Window Rendering (Single-Source)

Sub-windows display the contents of a nested composite blueprint when double-clicked. The rendering uses `nested.inline_def` directly:

```cpp
// For embedded subwindows, rebuild from inline_def (no group_id filtering needed)
const bp2::Blueprint::Nested* nested = bp.find_nested(group_iid);
if (nested && nested->inline_def) {
    visual::mutations::rebuild(win->scene, *nested->inline_def, interner, arena, "");
}
```

Each subwindow uses an `EmbeddedInlineHost` that reads/writes the authoritative `nested.inline_def` directly through the root `EditorModel`. There is no shadow copy — all mutations write through immediately via `replace_nested_preserve_order`.

### Simulation Integration

- **Signal keys** for embedded nodes use prefix: `"parent_id:local_node_id.port"` (e.g., `"GroundPower_1:src.v_out"`)
- **`triggerSwitch`/`setSliderValue`/`setKnobPosition`/`holdButton*`** accept an optional `group_id` parameter to build the prefixed simulation key
- **`updateNodeContentFromSimulation()`** iterates both root nodes and `inline_def` nodes
- **`buildEnergizedWireSet()`** handles embedded composite wires via `inline_def`

### Key Files

| File | Role |
|------|------|
| `document_components.cpp` | `addBlueprint()` — load, create collapsed+nested; `add_bridge_port_to_composite()` — single interface mutation |
| `document_simulation.cpp` | Simulation content updates, energized wires, signal interaction for both root and embedded nodes |
| `document_history.cpp` | Undo/redo — rebuilds window scenes from authoritative `inline_def` |
| `document_input.cpp` | `applyInputResult()` — dispatches input results (context menus, sim interactions) |
| `persist.cpp` | `load_blueprint_from_file_validated()` — JSON parsing and blueprint schema validation |
| `scene_mutations.cpp` | `rebuild()` — creates node/wire widgets from blueprint data |
| `blueprint_window.h` | `BlueprintWindow` — holds `EditingHost`, `external_blueprint`, etc. |
| `sub_window_renderer.cpp` | Sub-window viewport auto-fit logic |

### Common Pitfalls

1. **Library path lookup** — blueprints live in subdirectories (e.g., `library/systems/`). Use `registry.categories[blueprint_name]` to get the correct path.

2. **Blueprint loading** — blueprints are loaded in canonical v1 format; viewport state is not persisted (always resets to default when a blueprint is opened).

3. **Node lookup scope** — when looking up a node for an embedded composite, search `inline_def` (not the root blueprint). Use `find_node_in_scope()` helper in `document_simulation.cpp`.

## Content Types and Semantic Content Pipeline

### How Node Content Works

Node content is no longer implemented as per-type child widgets. The active pipeline is:

1. **Type definition** (`library/*.blueprint`) declares `content_type`
2. **Node layout** (`visual_node.cpp`) reserves content geometry inside `NodeWidget`
3. **Runtime updates** (`document.cpp::updateNodeContentFromSimulation()`) refresh cached content state on the node
4. **Semantic snapshot build** (`NodeWidget::refresh_content_semantic_snapshot()`) produces retained render and hit objects
5. **Rendering and interaction** both consume that same semantic snapshot

The key production object is `NodeWidget::content_semantic_snapshot()`. It contains:

- `render_objects` for live content painting
- `hit_objects` for semantic content interaction

`CanvasInput` enters content interactions through semantic hit testing, not widget type checks.

### Adding a New Content Type

**Step 1: Add the enum value** in `src/blueprint_v2/blueprint/node_content_type.h`.

**Step 2: Register the type string** in `src/editor/data/node_content.h` so blueprint metadata maps to the new `bp2::NodeContentType`.

**Step 3: Add layout support** in `src/editor/visual/node/visual_node.cpp`:
- update `preferred_content_size()`
- update `buildStandardLayout()` if the content needs reserved geometry there
- update `buildFourSidedLayout()` if the content needs reserved geometry there
- update `buildVerticalToggleLayout()` only if it needs the special vertical-toggle path

**Step 4: Add semantic rendering and interaction** in `src/editor/visual/node/visual_node.cpp`:
- extend `refresh_content_semantic_snapshot()` to emit the new render objects
- if interactive, extend `derive_content_interaction()` and emit the appropriate `InteractionBinding`

**Step 5: Wire up simulation readout** in `document.cpp::updateNodeContentFromSimulation()` so the node's cached content values are updated from runtime state.

**Step 6: Set `content_type` in the blueprint** (`library/category/MyComponent.blueprint`).

Critical pitfall:
Content geometry reservation and semantic snapshot emission must agree. If layout reserves space but no semantic render objects are emitted, the node will have empty content. If semantic hit objects are missing, the content will render but not interact.

### Color Format

ImGui uses AABBGGRR (packed uint32_t):
```cpp
uint32_t color = (alpha << 24) | (blue << 16) | (green << 8) | red;
// Example: opaque green = 0xFF00FF00 (alpha=FF, blue=00, green=FF, red=00)
```

The `NodeColor::to_uint32()` helper converts (r,g,b,a) floats to this format.

### Existing Content Types

| ContentType | Blueprint `content_type` | Rendered Semantically | Interactive? |
|-------------|-------------------------|-----------------------|-------------|
| Gauge | `"Gauge"` | Yes | No |
| Switch | `"Switch"` / `"HoldButton"` | Yes | Yes (click) |
| VerticalToggle | `"VerticalToggle"` | Yes | Yes (click) |
| Slider | `"Slider"` | Yes | Yes (drag scalar) |
| Indicator | `"Indicator"` | Yes | No |
| Knob | `"Knob"` | Yes | Yes (drag discrete) |
| Text | `"Text"` | Yes | No |

## KnobSwitch Port Semantics

- `KnobSwitch` terminals `wiper`, `throw1..throwN` are modeled as passive electrical contacts.
- Circuit usage is bidirectional at those terminals: `wiper -> throwN` and `throwN -> wiper` are both valid.
- `InOut` on these ports exists to represent passive contacts; it is not directed signal-flow meaning.
