# Extract to Blueprint Feature

## Overview

**Extract to Blueprint** allows users to select multiple nodes in the visual editor, right-click, and extract them into a new nested blueprint. The system automatically:
- Creates BlueprintInput/BlueprintOutput nodes for external connections
- Preserves internal wiring
- Replaces the selection with a single collapsed blueprint instance

---

## User Workflow

1. **Select nodes** using rectangular marquee (Alt+drag) or Ctrl+click
2. **Right-click** on any selected node
3. **Choose "Extract to Blueprint..."** from context menu
4. **Enter blueprint name** in popup dialog (default: `extracted_blueprint_N`)
5. **Confirm** - selection is replaced by collapsed blueprint node

### Result

```
BEFORE:                              AFTER:
┌─────────────────────────┐         ┌─────────────────────────┐
│ [ext]──►[node1]──►[ext] │         │ [ext]──►[collapsed]──►[ext]│
│         │    │         │   ──►   │            │            │
│         ▼    ▼         │         │      (expandable)        │
│       [node2] [node3]   │         └─────────────────────────┘
└─────────────────────────┘
```

Inside the collapsed blueprint:
```
┌──────────────────────────────┐
│ [BlueprintInput]──►[node1]   │
│        │          │          │
│        ▼          ▼          │
│     [node2]    [node3]       │
│        │          │          │
│        ▼          ▼          │
│ [BlueprintOutput]◄───────────│
└──────────────────────────────┘
```

---

## Data Structures

### ExternalConnection

Represents a wire that crosses the selection boundary.

```cpp
struct ExternalConnection {
    bool is_input;                    // true = BlueprintInput, false = BlueprintOutput
    ui::InternedId external_node_id;  // Node outside selection
    ui::InternedId external_port;     // Port on external node
    ui::InternedId internal_node_id;  // Node inside selection  
    ui::InternedId internal_port;     // Port on internal node
    std::string port_name;            // Name for interface port (derived from external_port)
    Domain domain;                    // Signal domain (Electrical, Logical, etc.)
    ui::InternedId original_wire_id;  // For removal
};
```

### ExtractionPlan

Complete analysis of what to extract.

```cpp
struct ExtractionPlan {
    std::vector<bp2::Blueprint::Node> internal_nodes;
    std::vector<bp2::Blueprint::Wire> internal_wires;
    std::vector<ExternalConnection> inputs;   // Connections from outside → inside
    std::vector<ExternalConnection> outputs;  // Connections from inside → outside
    float center_x;                           // Bounding box center
    float center_y;
};
```

### Command

```cpp
struct CmdExtractToBlueprint {
    std::vector<ui::InternedId> selected_node_ids;
    std::string new_blueprint_name;
    std::string group_id;  // Current group context (empty = root)
};
```

---

## Algorithm

### Phase 1: Analyze Selection

**Location**: `src/editor/commands/extract_blueprint.cpp`

```cpp
ExtractionPlan analyze_selection(
    const bp2::Blueprint& bp,
    const std::vector<ui::InternedId>& selected_ids,
    ui::StringInterner& interner)
{
    ExtractionPlan plan;
    
    // 1. Collect selected nodes
    std::unordered_set<ui::InternedId> selected_set(selected_ids.begin(), selected_ids.end());
    
    float min_x = FLT_MAX, min_y = FLT_MAX;
    float max_x = FLT_MIN, max_y = FLT_MIN;
    
    for (const auto& node : bp.nodes()) {
        if (selected_set.count(node.id)) {
            plan.internal_nodes.push_back(node);
            min_x = std::min(min_x, node.x);
            min_y = std::min(min_y, node.y);
            max_x = std::max(max_x, node.x + node.width.value_or(100));
            max_y = std::max(max_y, node.y + node.height.value_or(64));
        }
    }
    plan.center_x = (min_x + max_x) / 2;
    plan.center_y = (min_y + max_y) / 2;
    
    // 2. Classify wires
    for (const auto& wire : bp.wires()) {
        auto [src_node, src_port] = bp2_path_to_node_port(wire.source);
        auto [tgt_node, tgt_port] = bp2_path_to_node_port(wire.target);
        
        bool src_selected = selected_set.count(src_node);
        bool tgt_selected = selected_set.count(tgt_node);
        
        if (src_selected && tgt_selected) {
            // Internal wire - keep as-is
            plan.internal_wires.push_back(wire);
        } else if (src_selected && !tgt_selected) {
            // Output crossing boundary
            ExternalConnection ec;
            ec.is_input = false;
            ec.internal_node_id = src_node;
            ec.internal_port = src_port;
            ec.external_node_id = tgt_node;
            ec.external_port = tgt_port;
            ec.port_name = derive_port_name(tgt_port, interner);
            ec.domain = wire.domain;
            ec.original_wire_id = wire.id;
            plan.outputs.push_back(ec);
        } else if (!src_selected && tgt_selected) {
            // Input crossing boundary
            ExternalConnection ec;
            ec.is_input = true;
            ec.external_node_id = src_node;
            ec.external_port = src_port;
            ec.internal_node_id = tgt_node;
            ec.internal_port = tgt_port;
            ec.port_name = derive_port_name(src_port, interner);
            ec.domain = wire.domain;
            ec.original_wire_id = wire.id;
            plan.inputs.push_back(ec);
        }
        // else: both outside - ignore
    }
    
    // 3. Deduplicate port names
    deduplicate_port_names(plan.inputs);
    deduplicate_port_names(plan.outputs);
    
    return plan;
}
```

### Phase 2: Build Extracted Blueprint

```cpp
bp2::Blueprint build_extracted_blueprint(
    const ExtractionPlan& plan,
    const std::string& blueprint_id,
    ui::StringInterner& interner)
{
    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern(blueprint_id));
    bp = bp.with_display_name(blueprint_id);
    
    // Calculate offset to make coordinates relative to origin
    float offset_x = 0, offset_y = 0;
    if (!plan.internal_nodes.empty()) {
        offset_x = plan.internal_nodes[0].x;
        offset_y = plan.internal_nodes[0].y;
        for (const auto& n : plan.internal_nodes) {
            offset_x = std::min(offset_x, n.x);
            offset_y = std::min(offset_y, n.y);
        }
    }
    
    // 1. Copy internal nodes with repositioned coordinates
    for (auto node : plan.internal_nodes) {
        node.x -= offset_x + 200;  // Leave room for BlueprintInput nodes on left
        node.y -= offset_y;
        node.group_id = "";  // Clear group context
        bp = bp.with_node(node);
    }
    
    // 2. Add BlueprintInput nodes
    float input_y = 0;
    for (const auto& ec : plan.inputs) {
        bp2::Blueprint::Node input_node;
        input_node.id = interner.intern(ec.port_name);
        input_node.type = interner.intern("BlueprintInput");
        input_node.name = ec.port_name;
        input_node.x = 0;
        input_node.y = input_y;
        input_node.render_hint = "default";
        
        // Add ports: ext (input from parent), port (output to internal)
        EditorPort ext_port(interner.intern("ext"), PortSide::Input, PortType::Any);
        EditorPort port_port(interner.intern("port"), PortSide::Output, PortType::Any);
        input_node.inputs.push_back(ext_port);
        input_node.outputs.push_back(port_port);
        
        bp = bp.with_node(input_node);
        input_y += 80;
    }
    
    // 3. Add BlueprintOutput nodes
    float output_y = 0;
    float max_internal_x = 0;
    for (const auto& n : plan.internal_nodes) {
        max_internal_x = std::max(max_internal_x, n.x - offset_x - 200 + n.width.value_or(100));
    }
    
    for (const auto& ec : plan.outputs) {
        bp2::Blueprint::Node output_node;
        output_node.id = interner.intern(ec.port_name);
        output_node.type = interner.intern("BlueprintOutput");
        output_node.name = ec.port_name;
        output_node.x = max_internal_x + 200;
        output_node.y = output_y;
        output_node.render_hint = "default";
        
        // Add ports: port (input from internal), ext (output to parent)
        EditorPort port_port(interner.intern("port"), PortSide::Input, PortType::Any);
        EditorPort ext_port(interner.intern("ext"), PortSide::Output, PortType::Any);
        output_node.inputs.push_back(port_port);
        output_node.outputs.push_back(ext_port);
        
        bp = bp.with_node(output_node);
        output_y += 80;
    }
    
    // 4. Copy internal wires (adjust paths for relative coordinates)
    for (auto wire : plan.internal_wires) {
        // Paths stay the same since node IDs don't change
        bp = bp.with_wire(wire);
    }
    
    // 5. Add wires from BlueprintInput.port → internal node
    for (const auto& ec : plan.inputs) {
        bp2::Blueprint::Wire w;
        w.id = interner.intern("wire_" + ec.port_name + "_in");
        w.source = Path(interner.intern(ec.port_name), interner.intern("port"));
        w.target = Path(ec.internal_node_id, ec.internal_port);
        w.domain = ec.domain;
        bp = bp.with_wire(w);
    }
    
    // 6. Add wires from internal node → BlueprintOutput.port
    for (const auto& ec : plan.outputs) {
        bp2::Blueprint::Wire w;
        w.id = interner.intern("wire_" + ec.port_name + "_out");
        w.source = Path(ec.internal_node_id, ec.internal_port);
        w.target = Path(interner.intern(ec.port_name), interner.intern("port"));
        w.domain = ec.domain;
        bp = bp.with_wire(w);
    }
    
    // 7. Build interface
    bp2::Interface iface;
    for (const auto& ec : plan.inputs) {
        bp2::PortDescriptor pd;
        pd.name = interner.intern(ec.port_name);
        pd.direction = PortDirection::In;
        pd.domain = ec.domain;
        pd.type = PortType::Any;
        iface.ports.push_back(pd);
    }
    for (const auto& ec : plan.outputs) {
        bp2::PortDescriptor pd;
        pd.name = interner.intern(ec.port_name);
        pd.direction = PortDirection::Out;
        pd.domain = ec.domain;
        pd.type = PortType::Any;
        iface.ports.push_back(pd);
    }
    bp = bp.with_interface(iface);
    
    return bp;
}
```

### Phase 3: Build Nested Structure

```cpp
bp2::Blueprint::Nested build_nested(
    bp2::Blueprint&& extracted_bp,
    const ExtractionPlan& plan,
    const std::string& blueprint_id,
    ui::StringInterner& interner)
{
    bp2::Blueprint::Nested nested;
    nested.id = interner.intern(blueprint_id);
    nested.blueprint_id = interner.intern(blueprint_id);
    nested.embedded = true;
    nested.inline_def = std::make_unique<bp2::Blueprint>(std::move(extracted_bp));
    nested.x = plan.center_x;
    nested.y = plan.center_y;
    nested.iface = extracted_bp.iface();
    
    return nested;
}
```

### Phase 4: Modify Parent Blueprint

```cpp
void apply_extraction_to_parent(
    bp2::EditorModel& model,
    const ExtractionPlan& plan,
    const bp2::Blueprint::Nested& nested,
    const std::string& blueprint_id,
    ui::StringInterner& interner)
{
    // 1. Remove original nodes (this also removes connected wires)
    for (const auto& node : plan.internal_nodes) {
        model.remove_node(node.id);
    }
    
    // 2. Remove external wires (they'll be reconnected to nested)
    for (const auto& ec : plan.inputs) {
        model.remove_wire(ec.original_wire_id);
    }
    for (const auto& ec : plan.outputs) {
        model.remove_wire(ec.original_wire_id);
    }
    
    // 3. Add nested blueprint
    model.add_nested(bp2::Blueprint::Nested(nested));
    
    // 4. Add expandable collapsed node
    bp2::Blueprint::Node collapsed;
    collapsed.id = nested.id;
    collapsed.type = nested.blueprint_id;
    collapsed.name = blueprint_id;
    collapsed.expandable = true;
    collapsed.collapsed = true;
    collapsed.x = nested.x;
    collapsed.y = nested.y;
    collapsed.width = 160;
    collapsed.height = 64;
    collapsed.blueprint_path = blueprint_id;
    
    // Add ports from interface
    for (const auto& pd : nested.iface.ports) {
        EditorPort ep(pd.name, 
            pd.direction == PortDirection::In ? PortSide::Input : PortSide::Output,
            pd.type);
        if (pd.direction == PortDirection::In) {
            collapsed.inputs.push_back(ep);
        } else {
            collapsed.outputs.push_back(ep);
        }
    }
    
    model.add_node(collapsed);
    
    // 5. Reconnect external wires to nested blueprint
    for (const auto& ec : plan.inputs) {
        bp2::Blueprint::Wire w;
        w.id = interner.intern(model.allocate_wire_id());
        w.source = Path(ec.external_node_id, ec.external_port);
        w.target = Path(nested.id, interner.intern(ec.port_name));
        w.domain = ec.domain;
        model.add_wire(w);
    }
    
    for (const auto& ec : plan.outputs) {
        bp2::Blueprint::Wire w;
        w.id = interner.intern(model.allocate_wire_id());
        w.source = Path(nested.id, interner.intern(ec.port_name));
        w.target = Path(ec.external_node_id, ec.external_port);
        w.domain = ec.domain;
        model.add_wire(w);
    }
}
```

---

## Files to Create/Modify

### New Files

| File | Purpose |
|------|---------|
| `src/editor/commands/extract_blueprint.h` | ExtractionPlan, ExternalConnection structs, function declarations |
| `src/editor/commands/extract_blueprint.cpp` | All extraction logic (~400 LOC) |

### Modified Files

| File | Changes |
|------|---------|
| `src/editor/commands/commands.h` | Add `CmdExtractToBlueprint` struct to Command variant |
| `src/editor/commands/commands.cpp` | Add handler in `execute()` |
| `src/editor/visual/popups/context_menus.cpp` | Add "Extract to Blueprint..." menu item |
| `src/editor/window_system.h` | Add `PendingExtractToBlueprint` struct |
| `src/editor/input/input_types.h` | Add `show_extract_dialog` to InputResult (optional) |
| `src/editor/app/editor_app.cpp` | Handle extraction dialog popup |

---

## UI Flow

### Context Menu Entry

```cpp
// context_menus.cpp - in renderNodeContext()

// Check if multiple nodes are selected
auto& selected = doc->input().selected_node_ids();
if (selected.size() >= 2 && !is_read_only) {
    ImGui::Separator();
    if (ImGui::MenuItem("Extract to Blueprint...")) {
        ws.pendingExtract.show = true;
        ws.pendingExtract.node_ids = selected;
        ws.pendingExtract.group_id = ws.nodeContextMenu.group_id;
        ws.pendingExtract.suggested_name = generate_suggested_name(ws);
    }
}
```

### Name Input Dialog

```cpp
// editor_app.cpp - in main loop

if (ws.pendingExtract.show) {
    ImGui::OpenPopup("Extract to Blueprint");
    ws.pendingExtract.show = false;
}

if (ImGui::BeginPopupModal("Extract to Blueprint", nullptr, 
    ImGuiWindowFlags_AlwaysAutoResize)) {
    
    static char name_buf[128] = "";
    if (ImGui::IsWindowAppearing()) {
        strncpy(name_buf, ws.pendingExtract.suggested_name.c_str(), 127);
        ImGui::SetKeyboardFocusHere();
    }
    
    ImGui::Text("Enter blueprint name:");
    ImGui::InputText("##name", name_buf, sizeof(name_buf));
    
    ImGui::Separator();
    
    if (ImGui::Button("Extract")) {
        Document* doc = ws.findDocumentById(ws.pendingExtract.source_doc_id);
        if (doc) {
            doc->extractToBlueprint(
                ws.pendingExtract.node_ids,
                std::string(name_buf),
                ws.pendingExtract.group_id
            );
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }
    
    ImGui::EndPopup();
}
```

---

## Edge Cases

| Case | Handling |
|------|----------|
| **Selection contains existing BlueprintInput/Output** | Copy as regular nodes; don't treat specially |
| **Selection contains expandable (nested) node** | Copy the `Nested` entry to new blueprint's nested list |
| **Bidirectional port (InOut)** | Create separate Input and Output with same name? Or single bidirectional port? |
| **Multiple wires to same external port** | Deduplicate - single BlueprintInput/Output per unique external port |
| **No external connections** | Valid - create blueprint with empty interface |
| **Single node selected** | Disable menu item (require ≥2 nodes) |
| **Wires between selected nodes in different groups** | Keep as internal, preserve group_id on nodes |
| **Domain mismatch** | Use wire's domain; if conflict, prefer Electrical |

---

## Port Naming Strategy

Port names are derived from the external port they connect to:

```cpp
std::string derive_port_name(ui::InternedId port_id, ui::StringInterner& interner) {
    std::string_view sv = interner.resolve(port_id);
    return std::string(sv);  // Use external port name directly
}

void deduplicate_port_names(std::vector<ExternalConnection>& connections) {
    std::unordered_map<std::string, int> counts;
    for (auto& ec : connections) {
        std::string base = ec.port_name;
        if (counts[base]++ > 0) {
            ec.port_name = base + "_" + std::to_string(counts[base]);
        }
    }
}
```

Example: If two internal nodes connect to external `v_bus`:
- First becomes interface port `v_bus`
- Second becomes interface port `v_bus_2`

---

## Testing Strategy

### Unit Tests

```cpp
// tests/extract_blueprint_tests.cpp

TEST(ExtractBlueprintTest, BasicExtraction) {
    // Create blueprint with 3 connected nodes
    // Select 2 of them
    // Extract
    // Verify: new nested has 2 internal nodes + correct I/O
}

TEST(ExtractBlueprintTest, MultipleInputsOutputs) {
    // Node with 2 inputs from outside, 2 outputs to outside
    // Extract single node
    // Verify: 2 BlueprintInput, 2 BlueprintOutput
}

TEST(ExtractBlueprintTest, InternalWiresPreserved) {
    // A -> B -> C, select A and B
    // Verify: wire A->B is internal in new blueprint
}

TEST(ExtractBlueprintTest, NestedNodeExtraction) {
    // Select a collapsed nested blueprint node
    // Verify: nested definition is copied correctly
}

TEST(ExtractBlueprintTest, EmptyInterface) {
    // 2 connected nodes with no external connections
    // Extract creates blueprint with empty interface
}
```

### Integration Tests

- Extract, then expand and verify internal structure
- Extract, save file, reload, verify persistence
- Undo extraction, verify original state restored

---

## Future Enhancements

1. **Preview mode**: Show ghost outline of what will be extracted before confirming
2. **Port type inference**: Derive port types from connected signals instead of `Any`
3. **Auto-layout**: Arrange BlueprintInput/Output nodes based on wire routing
4. **Save to library**: Option to save extracted blueprint as standalone `.blueprint` file
5. **Batch extraction**: Select multiple groups and extract each to separate blueprints

---

## Implementation Order

1. **Phase 1**: Core extraction logic (`extract_blueprint.cpp`)
   - `analyze_selection()`
   - `build_extracted_blueprint()`
   - `build_nested()`
   - `apply_extraction_to_parent()`

2. **Phase 2**: Command integration
   - Add `CmdExtractToBlueprint` to variant
   - Implement `execute()` handler
   - Add `Document::extractToBlueprint()` wrapper

3. **Phase 3**: UI integration
   - Context menu entry
   - Name input dialog
   - Pending state in WindowSystem

4. **Phase 4**: Testing
   - Unit tests for each phase
   - Integration tests
   - Manual QA

---

## Estimated Effort

| Component | Lines of Code | Time |
|-----------|---------------|------|
| extract_blueprint.h/cpp | ~400 LOC | 4h |
| Command integration | ~80 LOC | 1h |
| UI integration | ~100 LOC | 2h |
| Tests | ~200 LOC | 2h |
| **Total** | ~780 LOC | 9h |
