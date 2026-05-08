# Visual Editor

## Architecture Overview

```
┌───────────────────────────────────────────────────────────────────────┐
│ WindowSystem (manages all documents + global panels)                 │
├───────────────────────────────────────────────────────────────────────┤
│ Document (one per tab/file)                                          │
│ ├── EditorModel (bp2::EditorModel) — blueprint data                  │
│ ├── StringInterner + PathArena — string interning                    │
│ ├── SimulationBridge — owns Simulator, signal caches                 │
│ ├── WindowManager — owns windows, scenes, viewports                  │
│ └── RenderingResources — fonts, textures, shaders                    │
│     └── BlueprintWindow[] — MDI windows                              │
│         ├── Scene (widget tree)                                      │
│         │   ├── NodeWidget (component nodes)                          │
│         │   │   ├── Ports                                             │
│         │   │   └── Content widgets (gauges, switches)               │
│         │   └── Wire paths                                           │
│         ├── Viewport (pan/zoom/grid)                                 │
│         └── CanvasInput (mouse/keyboard FSM)                         │
├───────────────────────────────────────────────────────────────────────┤
│ Global Panels: Inspector, PropertiesWindow, ScriptEditorWindow,    │
│ Oscilloscope, PI-ZN Tuner                                            │
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
    ScriptEditorWindow script_editor_window_;
    ComponentRegistry type_registry_;
    bp2::LibraryIndex library_index_;
    editor::RenderingResources rendering_resources_;
    OscilloscopeModel oscilloscope_;
    PiZnTuner pi_zn_tuner_;

public:
    Document& createDocument();
    Document* openDocument(const std::string& path);
    bool closeDocument(Document& doc);

    Document* activeDocument();
    void setActiveDocument(Document* doc);

    Inspector& inspector();
    PropertiesWindow& propertiesWindow();
    ScriptEditorWindow& scriptEditorWindow();
    ComponentRegistry& typeRegistry();
    const bp2::LibraryIndex& libraryIndex() const;
    editor::RenderingResources& renderingResources();
};
```

File: `src/editor/window_system.h`

### Document
Single open document — owns blueprint, simulation, and windows. Uses delegation:
- **SimulationBridge** — owns simulator, signal caches, interaction binding
- **WindowManager** — owns windows, scenes, viewports
- **EditorModel** — owns blueprint state, undo/redo

```cpp
class Document {
    editor::DocumentId id_;
    std::string filepath_;
    std::string display_name_;

    ui::StringInterner interner_;
    bp2::PathArena arena_{interner_};
    bp2::EditorModel model_;
    WindowManager window_manager_;
    SimulationBridge simulation_bridge_;

public:
    explicit Document(const ComponentRegistry* type_registry = nullptr,
                      const bp2::LibraryIndex* library_index = nullptr,
                      const editor::RenderingResources* rendering_resources = nullptr);

    const editor::DocumentId& id() const;
    const std::string& filepath() const;
    const std::string& displayName() const;
    std::string title() const;
    bool isPristine() const;

    bool save(const std::string& path);
    bool load(const std::string& path);

    bool saveWorkspaceSession();
    bool loadWorkspaceSession();

    bp2::Blueprint const& blueprint() const;
    bp2::EditorModel& model();
    SimulationBridge& simulation();
    WindowManager& windows();

    void rebuildWindows();
    void rebuildSimulation();
    void tickSimulation(double dt);
};
```

File: `src/editor/document.h`

### SimulationBridge
Encapsulates simulator lifecycle and signal cache:
```cpp
class SimulationBridge {
    Simulator<JIT_Solver> simulator_;
    std::vector<float> signal_cache_;
    std::unordered_map<core::InternedId, float> overrides_;
    std::unordered_set<core::InternedId> held_buttons_;

public:
    void rebuild(const bp2::Blueprint& blueprint,
                 const ComponentRegistry& registry,
                 const bp2::LibraryIndex* library_index);
    void tick(double dt);
    float get_signal(core::InternedId key) const;
    void set_override(core::InternedId key, float value);
    void clear_override(core::InternedId key);
};
```

File: `src/editor/simulation_bridge.h`

### Scene & Widgets
The editor uses two scene systems:
1. **Editor Scene** (`src/editor/visual/scene.h`) — editor-specific visual nodes
2. **UI Framework** (`src/ui/core/scene.h`) — generic widget tree (see `knowledge/11_ui_framework.md`)

### Node Factory
Selects visual node class by `render_hint`:
- `bus` → `BusNodeWidget`
- `ref` → `RefNodeWidget`
- `group` → `GroupNodeWidget`
- `text` → `TextNodeWidget`
- default → `NodeWidget` (can expose semantic content interactions)

File: `src/editor/visual/node/node_factory.h`

### Canvas Input
Mouse/keyboard FSM for editing interactions:
- `CanvasInput` — main input coordinator
- `editing_host.h` — edit command host
- Semantic content interaction: `Click`, `DragScalar`, `DragDiscrete`

Files: `src/editor/input/canvas_input.h`, `src/editor/input/editing_host.h`

### Workspace Session Persistence
Separate from canonical blueprint persistence:
- `src/editor/visual/workspace_session_persist.cpp`
- Stores viewport/window/session state only
- Per-node authored color lives in canonical blueprint, not session

## Files

| File | Purpose |
|------|---------|
| `src/editor/window_system.h` | WindowSystem |
| `src/editor/document.h` | Document |
| `src/editor/simulation_bridge.h` | SimulationBridge |
| `src/editor/visual/scene.h` | Editor scene |
| `src/editor/visual/scene_mutations.h` | Scene mutations |
| `src/editor/input/canvas_input.h` | Canvas input FSM |
| `src/editor/input/editing_host.h` | Edit command host |
| `src/editor/window/window_manager.h` | WindowManager |
| `src/editor/window/blueprint_window.h` | BlueprintWindow |
| `src/editor/visual/inspector/inspector.h` | Inspector panel |
| `src/editor/window/properties_window.h` | Properties panel |
| `src/editor/window/script_editor_window.h` | Script editor panel |
| `src/editor/oscilloscope.h` | Oscilloscope panel |
| `src/editor/pi_zn_tuner.h` | PI-ZN tuner |
| `src/editor/visual/workspace_session_persist.cpp` | Session persistence |
