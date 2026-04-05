# Value Node Rendering and Interaction Analysis

## Key Finding: Why Clicking Value Node Enters DraggingKnob State

### Root Cause
Value nodes that are created programmatically **without `render_hint="ref"`** get classified as standard `NodeWidget` instances instead of `RefNodeWidget` instances. This causes them to render with interactive content widgets (like Knob), which makes clicks enter `DraggingKnob` state instead of `DraggingNode` state.

---

## Node Type Selection (node_factory.h:22-46)

The factory selects widget types based on `render_hint`:

```cpp
static std::unique_ptr<Widget> create(const bp2::Blueprint::Node& node, ...) {
    if (node.render_hint == "bus")      → BusNodeWidget
    if (node.render_hint == "ref")      → RefNodeWidget    ← Value nodes should use this
    if (node.render_hint == "group")    → GroupNodeWidget
    if (node.render_hint == "text")     → TextNodeWidget
    default                              → NodeWidget       ← Generic component nodes
}
```

---

## Content Type Mapping (node_content.h:93-147)

When a `NodeWidget` is created, it checks the `TypeDefinition.content_type`:

| content_type | Creates              | Interaction Type |
|--------------|----------------------|------------------|
| "Gauge"      | VoltmeterWidget      | None (read-only) |
| "Switch"     | SwitchWidget         | Toggle           |
| "VerticalToggle" | VerticalToggleWidget | Toggle          |
| "Text"       | Label                | None (read-only) |
| "Slider"     | SliderWidget         | Slider           |
| "Indicator"  | IndicatorWidget      | None (read-only) |
| **"Knob"**   | **KnobWidget**       | **Knob ← ISSUE** |
| (None/unspecified) | Spacer (no interaction) | None |

---

## Interaction Hit Detection (visual_node.cpp:358-387)

When user clicks a `NodeWidget`, `try_handle_node_interaction()` is called:

```cpp
std::optional<NodeInteractionHit> NodeWidget::query_interaction(Pt world_pos) {
    if (!content_widget_) return std::nullopt;  // No content = no interaction
    
    // Map content widget type to interaction:
    if (dynamic_cast<SliderWidget*>(content_widget_))
        return NodeInteractionType::Slider;
    if (dynamic_cast<KnobWidget*>(content_widget_))
        return NodeInteractionType::Knob;     ← This is returned for Knob widgets
    if (content_widget_->isToggleable())
        return NodeInteractionType::Toggle;
}
```

---

## Mouse Down Flow (canvas_input_mouse_down.cpp:73-77)

When user left-clicks:

```cpp
auto* hn = std::get_if<visual::HitNode>(&hit);
if (try_handle_node_interaction(hn->widget, world, result)) {
    // If interaction found (Slider/Knob/Toggle), enter that state and return
    return result;
}
// Only if NO interaction: enter_drag_node()
enter_drag_node(hn->widget, false, mods.ctrl);
```

---

## Interaction → State Transition (canvas_input.cpp:295-325)

```cpp
bool CanvasInput::try_handle_node_interaction(...) {
    auto* node_widget = dynamic_cast<NodeWidget*>(widget);
    if (!node_widget) return false;  // Fails for RefNodeWidget (doesn't inherit NodeWidget)
    
    const auto interaction = node_widget->query_interaction(world);
    
    switch (interaction->type) {
        case NodeInteractionType::Slider:
            enter_drag_slider(widget, ...);   // → InputState::DraggingSlider
            return true;
        case NodeInteractionType::Knob:
            enter_drag_knob(widget, ...);     // → InputState::DraggingKnob ← WRONG STATE
            return true;
        case NodeInteractionType::Toggle:
            result.toggle_switch_node_id = ...;
            return true;
    }
    return false;
}
```

---

## The Problem: Value Node Default Content

### Programmatic Value Node Creation (test_fixtures.h:194-202)

```cpp
inline TypeDefinition make_value_type() {
    TypeDefinition td;
    td.classname = "Value";
    td.cpp_class = true;
    td.ports["o"] = Port{PortDirection::Out, PortType::Any, std::nullopt};
    // NOTE: NO content_type specified!
    // NOTE: NO render_hint specified!
    return td;
}
```

### Result
- `content_type` defaults to empty string (not one of: "Gauge", "Switch", "Text", "Slider", "Knob")
- `render_hint` defaults to empty string (not "ref")
- `NodeWidget` is created instead of `RefNodeWidget`
- Content widget is `Spacer` (since no recognized content_type)
- `query_interaction()` returns empty (no interactive content)
- Falls through to `enter_drag_node()` ✓ Correct behavior

---

## But WAIT: The Knob Issue

If a Value node somehow has `content_type: "Knob"` set, then:

1. `NodeWidget` is created (not `RefNodeWidget`)
2. `KnobWidget` is created for the content area
3. Clicking the content area triggers `NodeInteractionType::Knob`
4. `enter_drag_knob()` is called → `InputState::DraggingKnob` ← WRONG STATE
5. Should be `DraggingNode` for dragging the node itself

---

## Blueprint Specification (test_blueprint_loading.cpp:509-528)

The test defines the **REQUIRED** structure for Value nodes in blueprints:

```json
{
  "type": "Value",
  "id": "value1",
  "render_hint": "ref",        ← MUST be "ref"
  "x": 100, "y": 100
}
```

**Contract**: All Value nodes in `.blueprint` files must have `render_hint="ref"`.

---

## RefNodeWidget vs NodeWidget

### RefNodeWidget (render_hint="ref")
- **Single centered port**, minimal box + text rendering
- **NO query_interaction() method** (not interactive in editor)
- Cannot enter `DraggingKnob` state
- Used for: RefNode (ground), Value nodes

### NodeWidget (default)
- **Standard component node** with full layout (header, ports, content, type name)
- **Has query_interaction()** that can return Slider/Knob/Toggle
- Can enter state based on content widget type
- Used for: Battery, Splitter, Clamp, etc.

---

## Content Widget Rendering (node_content_renderer.cpp)

The renderer handles different node types:

```cpp
void NodeContentRenderer::render(...) {
    if (content_type == NodeContentType::Gauge)
        renderGauge(node, width, readOnly);
    else if (content_type == NodeContentType::Slider)
        renderSlider(node, width, readOnly);
    else if (content_type == NodeContentType::Knob)
        renderKnob(node, width, readOnly);
    else if (content_type == NodeContentType::Switch)
        renderSwitch(node, width, readOnly);
    else if (content_type == NodeContentType::Value)
        renderValue(node, width, readOnly);  ← Text display only
    // ... others
}
```

**NodeContentType::Value** is a **read-only text display** (not interactive).

---

## Summary: Why DraggingKnob Instead of DraggingNode?

1. **Programmatically created Value nodes** lack `render_hint="ref"`
2. **Factory creates NodeWidget** instead of RefNodeWidget
3. **If content_type="Knob"** is set, KnobWidget is created
4. **Clicking triggers KnobWidget interaction** → `NodeInteractionType::Knob`
5. **enter_drag_knob() is called** → `InputState::DraggingKnob`
6. **Should be enter_drag_node()** → `InputState::DraggingNode`

### Fix
Either:
- A) Set `render_hint="ref"` for Value nodes (creates RefNodeWidget, no interaction)
- B) Remove `content_type="Knob"` from Value nodes (creates Spacer, no interaction)
- C) Both (recommended for compliance with test_blueprint_loading.cpp)

---

## Render Hints (Summary)

| render_hint | Widget Type | Purpose | Has query_interaction |
|-------------|-------------|---------|----------------------|
| "bus"       | BusNodeWidget | Electrical bus | No |
| "ref"       | RefNodeWidget | Ref/Value node | No |
| "group"     | GroupNodeWidget | Container | No |
| "text"      | TextNodeWidget | Annotation | No |
| (default)   | NodeWidget | Component | Yes (if content) |

---

## Content Types (Summary)

| type | widget | interactive |
|------|--------|-------------|
| None | Spacer | No |
| Gauge | Voltmeter | No |
| Switch | Switch button | Yes (Toggle) |
| VerticalToggle | Slider | Yes (Toggle) |
| Value | Label | No |
| Text | Label | No |
| Slider | Slider handle | Yes (Slider) |
| Indicator | LED light | No |
| Knob | Rotary switch | Yes (Knob) |

---

## Key Code Locations

| Location | Role |
|----------|------|
| `src/editor/visual/node/node_factory.h:22-46` | Widget type selection based on render_hint |
| `src/editor/data/node_content.h:93-147` | Content widget creation from TypeDefinition |
| `src/editor/visual/node/visual_node.cpp:358-387` | NodeWidget.query_interaction() → NodeInteractionType |
| `src/editor/input/canvas_input_mouse_down.cpp:73-77` | Mouse down dispatch to interaction or drag |
| `src/editor/input/canvas_input.cpp:283-328` | try_handle_node_interaction() state machine |
| `tests/test_blueprint_loading.cpp:509-528` | Blueprint convention test for Value nodes |
| `tests/test_fixtures.h:194-202` | Programmatic Value type definition |
