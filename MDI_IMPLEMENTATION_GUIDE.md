# MDI Editor Implementation Guide — For Coding Agent

**Project**: AN-24 Blueprint Editor — SDI → MDI Migration  
**Date**: 2026-03-11  
**Purpose**: Step-by-step instructions for implementing MDI with correct knowledge of the actual codebase

---

## 📋 Overview

Convert the editor from Single-Document Interface (SDI) where one `EditorApp` owns one `Blueprint` + `Simulator`, to Multi-Document Interface (MDI) where multiple documents can be open simultaneously as ImGui dockable tabs.

**Current architecture** (what exists now):

```
main()  →  EditorApp app;  (stack object in an24_editor.cpp)
EditorApp {
    Blueprint blueprint;                          // ONE blueprint
    WindowManager window_manager{blueprint};      // root + sub-windows
    VisualScene& scene;                           // root window scene
    CanvasInput& input;                           // root window input
    WireManager& wire_manager;                    // root window wire mgr
    Simulator<JIT_Solver> simulation; // ONE simulation
    Inspector inspector;                          // ONE inspector
    PropertiesWindow properties_window;           // ONE properties
    TypeRegistry type_registry;             // loaded from library/*.json
    // context menu, color picker state...
}
```

**Target architecture** (what to build):

```
main()  →  WindowSystem ws;  (stack object in an24_editor.cpp)
WindowSystem {
    vector<unique_ptr<Document>> documents_;      // MANY documents
    Document* active_document_;                   // currently focused
    InspectorPanel inspector_;                    // global, switches to active doc
    PropertiesPanel properties_;                  // global modal
    TypeRegistry type_registry_;            // shared read-only
    // context menu, color picker state (with source document pointer)
}

Document {
    Blueprint blueprint;
    WindowManager window_manager{blueprint};
    Simulator<JIT_Solver> simulation;
    bool simulation_running;
    string filepath, display_name;
    bool modified;
    unordered_map<string, float> signal_overrides;
    unordered_set<string> held_buttons;
}
```

---

## ⚠️ Critical Codebase Facts (Must Know Before Coding)

### 1. Naming Conventions — Use camelCase for Methods

The codebase consistently uses **camelCase** for methods:

- `markDirty()`, `consumeSelection()`, `buildDisplayTree()`, `selectNodeById()`
- `addNode()`, `removeWire()`, `hitTestPorts()`, `clamp_zoom()`
- `applyAndClose()`, `cancelAndClose()`, `renderTableParam()`

**Exception**: `Blueprint` struct uses `snake_case` for its methods:

- `add_node()`, `add_wire()`, `find_node()`, `rebuild_wire_index()`, `recompute_group_ids()`

**Rule**: Follow the existing convention of the file/class you're modifying. New classes should use camelCase for methods.

### 2. Structs vs Classes

The codebase mixes both:

- `struct EditorApp`, `struct BlueprintWindow`, `struct Blueprint`, `struct Node` — public by default
- `class VisualScene`, `class Inspector`, `class PropertiesWindow` — private by default with public API

**Rule**: Use `class` for new types with invariants. Use `struct` only for plain data.

### 3. Simulator Types — Two Exist, Use the Right One

- `Simulator<JIT_Solver>` — Template class in `src/jit_solver/simulator.h`. **This is what EditorApp uses**.
- `SimulationController` — Older wrapper in `src/editor/simulation.h`. Used in tests only, NOT in editor.

**Rule**: `Document` must use `Simulator<JIT_Solver>`, same as current `EditorApp`.

### 4. Save/Load Is in VisualScene, Not Blueprint

Currently `VisualScene::save(path)` and `VisualScene::load(path)` handle persistence:

```cpp
// In scene.h
bool save(const char* path) {
    bp_->pan = vp_.pan; bp_->zoom = vp_.zoom; bp_->grid_step = vp_.grid_step;
    return save_blueprint_to_file(*bp_, path);
}
bool load(const char* path) {
    auto bp = load_blueprint_from_file(path);
    if (!bp.has_value()) return false;
    *bp_ = std::move(*bp);
    bp_->rebuild_wire_index();
    vp_.pan = bp_->pan; vp_.zoom = bp_->zoom; vp_.grid_step = bp_->grid_step;
    cache_.clear(); invalidateSpatialGrid();
    return true;
}
```

The free functions are in `src/editor/visual/scene/persist.h`:

```cpp
bool save_blueprint_to_file(const Blueprint& bp, const char* path);
std::optional<Blueprint> load_blueprint_from_file(const char* path);
```

**Rule**: `Document::save()` and `Document::load()` should delegate to `save_blueprint_to_file` / `load_blueprint_from_file`, same pattern as `VisualScene`.

### 5. ImGuiDrawList Adapter Lives in an24_editor.cpp

The `ImGuiDrawList : public IDrawList` adapter is defined locally in `examples/an24_editor.cpp`. It wraps `ImDrawList*` from ImGui.

**Rule**: Move `ImGuiDrawList` to a header (`src/editor/imgui_draw_list.h`) so `DocumentWindow` rendering can use it.

### 6. process_window Lambda — The Core Rendering Logic

The 170-line `process_window` lambda in `an24_editor.cpp` is the unified renderer + input handler for any `BlueprintWindow`. It:

1. Updates hover state
2. Renders grid + blueprint + wires + nodes
3. Renders ImGui widget overlays (checkboxes, sliders, progress bars)
4. Renders marquee selection
5. Handles mouse/keyboard input via CanvasInput FSM
6. Calls `app.apply_input_result()` to dispatch actions

**This is the most complex piece to refactor**. It must be extracted to a reusable function.

### 7. TypeRegistry Is Read-Only After Load

`TypeRegistry` is loaded once at startup via `load_type_registry()`. After that it's only read. Safe to share across documents.

### 8. Inspector Takes `const VisualScene&`

```cpp
class Inspector {
    explicit Inspector(const VisualScene& scene);
    // ...
};
```

When switching active document, the inspector needs to point to a different scene. Currently there's no `setScene()` method — **you'll need to add one**.

### 9. BlueprintWindow Members Are Public

```cpp
struct BlueprintWindow {
    VisualScene scene;
    WireManager wire_manager;
    CanvasInput input;
    // ...
};
```

Not movable or copyable (holds references). Created via `WindowManager::open()`.

### 10. No ImGui Docking Currently

The current build does **NOT** use ImGui docking branch. The inspector is manually positioned/sized with hard-coded coordinates and a custom splitter. Check if the imgui dependency includes docking support before relying on `ImGui::DockSpace`.

---

## 🏗️ Implementation Plan — 7 Phases

### Phase 1: Preparation — Move ImGuiDrawList, Add Inspector::setScene

**Files to create:**

- `src/editor/imgui_draw_list.h`

**Files to modify:**

- `examples/an24_editor.cpp` — remove `ImGuiDrawList` class, `#include "editor/imgui_draw_list.h"`
- `src/editor/visual/inspector/inspector.h` — add `setScene(const VisualScene&)` method

**Steps:**

1.1. Extract `ImGuiDrawList` class from `examples/an24_editor.cpp` (lines ~53-132) into `src/editor/imgui_draw_list.h`:

```cpp
#pragma once
#include "visual/renderer/draw_list.h"
#include "data/pt.h"
#include <imgui.h>
// Copy the full ImGuiDrawList class definition here, exactly as-is
```

1.2. In `examples/an24_editor.cpp`, replace the class definition with:

```cpp
#include "editor/imgui_draw_list.h"
```

1.3. Add `setScene()` to `Inspector`:

```cpp
// In inspector.h, add public method:
void setScene(const VisualScene& scene) {
    scene_ = &scene;  // Note: scene_ is currently `const VisualScene& scene_`
    markDirty();
}
```

**IMPORTANT**: `scene_` is currently a reference (`const VisualScene& scene_`). You need to change it to a pointer (`const VisualScene* scene_`) and update all usages (`scene_.` → `scene_->`) in `inspector_core.cpp` and `inspector_render.cpp`.

1.4. Verify build compiles: `cmake --build build --target an24_editor`

---

### Phase 2: Create Document Class

**Files to create:**

- `src/editor/document.h`
- `src/editor/document.cpp`

`Document` encapsulates everything that was per-document in `EditorApp`. It does NOT render — that stays in the main loop.

```cpp
// src/editor/document.h
#pragma once

#include "data/blueprint.h"
#include "window/window_manager.h"
#include "visual/scene/scene.h"
#include "visual/scene/persist.h"
#include "input/canvas_input.h"
#include "visual/scene/wire_manager.h"
#include "jit_solver/simulator.h"
#include "json_parser/json_parser.h"
#include <string>
#include <unordered_map>
#include <unordered_set>

/// A single open document: owns a Blueprint + Simulator + WindowManager.
/// Multiple Document instances can coexist for MDI.
class Document {
public:
    /// Create new untitled document
    Document();

    /// Non-copyable, non-movable (owns WindowManager which holds references)
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;
    Document(Document&&) = delete;
    Document& operator=(Document&&) = delete;

    // ── Identity ──

    const std::string& id() const { return id_; }
    const std::string& filepath() const { return filepath_; }
    const std::string& displayName() const { return display_name_; }

    /// Title for ImGui tab: "filename.json*" if modified
    std::string title() const;

    // ── Modified tracking ──

    bool isModified() const { return modified_; }
    void markModified() { modified_ = true; }
    void clearModified() { modified_ = false; }

    // ── File I/O ──

    bool save(const std::string& path);
    bool load(const std::string& path);

    // ── Blueprint & window access ──

    Blueprint& blueprint() { return blueprint_; }
    const Blueprint& blueprint() const { return blueprint_; }

    WindowManager& windowManager() { return window_manager_; }
    const WindowManager& windowManager() const { return window_manager_; }

    /// Root window convenience accessors
    BlueprintWindow& root() { return window_manager_.root(); }
    VisualScene& scene() { return root().scene; }
    CanvasInput& input() { return root().input; }
    WireManager& wireManager() { return root().wire_manager; }

    // ── Simulation ──

    Simulator<JIT_Solver>& simulation() { return simulation_; }
    const Simulator<JIT_Solver>& simulation() const { return simulation_; }
    bool isSimulationRunning() const { return simulation_running_; }

    void startSimulation();
    void stopSimulation();
    void rebuildSimulation();
    void updateSimulationStep(float dt);

    /// Update node_content (gauges, switches, etc.) from simulation values.
    /// Needs TypeRegistry for reset_node_content logic.
    void updateNodeContentFromSimulation();
    void resetNodeContent(const TypeRegistry& registry);

    // ── Signal overrides (switch/button clicks) ──

    std::unordered_map<std::string, float>& signalOverrides() { return signal_overrides_; }
    std::unordered_set<std::string>& heldButtons() { return held_buttons_; }

    void triggerSwitch(const std::string& node_id);
    void holdButtonPress(const std::string& node_id);
    void holdButtonRelease(const std::string& node_id);

    // ── Component/blueprint addition ──

    void addComponent(const std::string& classname, Pt world_pos,
                      const std::string& group_id,
                      TypeRegistry& registry);
    void addBlueprint(const std::string& blueprint_name, Pt world_pos,
                      const std::string& group_id,
                      TypeRegistry& registry);

    // ── Sub-windows ──

    void openSubWindow(const std::string& collapsed_group_id);

    // ── Input result dispatch ──

    /// Apply input result from any window's CanvasInput.
    /// Updates context menu state, rebuilds simulation, etc.
    /// Returns true if context menu / node menu should be shown (caller handles ImGui popup).
    struct InputResultAction {
        bool show_context_menu = false;
        Pt context_menu_pos;
        std::string context_menu_group_id;

        bool show_node_context_menu = false;
        size_t context_menu_node_index = 0;
        std::string node_context_menu_group_id;
    };
    InputResultAction applyInputResult(const InputResult& r, const std::string& group_id = "");

private:
    std::string id_;
    std::string filepath_;
    std::string display_name_ = "Untitled";
    bool modified_ = false;

    Blueprint blueprint_;
    WindowManager window_manager_{blueprint_};
    Simulator<JIT_Solver> simulation_;
    bool simulation_running_ = false;

    std::unordered_map<std::string, float> signal_overrides_;
    std::unordered_set<std::string> held_buttons_;

    static int next_id_;
};
```

**Implementation** (`document.cpp`): Copy method bodies from `app.cpp` and adapt:

- `Document::Document()`: Set `id_ = "doc_" + std::to_string(next_id_++);`
- `Document::title()`: Return `display_name_ + (modified_ ? "*" : "")`
- `Document::save(path)`:
  ```cpp
  bool Document::save(const std::string& path) {
      // Sync viewport state into blueprint before saving
      auto& vp = scene().viewport();
      blueprint_.pan = vp.pan;
      blueprint_.zoom = vp.zoom;
      blueprint_.grid_step = vp.grid_step;
      if (!save_blueprint_to_file(blueprint_, path.c_str())) return false;
      filepath_ = path;
      // Extract filename for display
      auto pos = path.find_last_of("/\\");
      display_name_ = (pos != std::string::npos) ? path.substr(pos + 1) : path;
      clearModified();
      return true;
  }
  ```
- `Document::load(path)`:
  ```cpp
  bool Document::load(const std::string& path) {
      auto bp = load_blueprint_from_file(path.c_str());
      if (!bp.has_value()) return false;
      blueprint_ = std::move(*bp);
      blueprint_.rebuild_wire_index();
      auto& vp = scene().viewport();
      vp.pan = blueprint_.pan;
      vp.zoom = blueprint_.zoom;
      vp.grid_step = blueprint_.grid_step;
      vp.clamp_zoom();
      scene().clearCache();
      scene().invalidateSpatialGrid();
      filepath_ = path;
      auto pos = path.find_last_of("/\\");
      display_name_ = (pos != std::string::npos) ? path.substr(pos + 1) : path;
      clearModified();
      return true;
  }
  ```
- `startSimulation()`, `stopSimulation()`, `rebuildSimulation()`, `updateSimulationStep()` — copy from `EditorApp` methods in `app.h`, replacing `simulation` with `simulation_` and `scene` with `scene()`.
- `updateNodeContentFromSimulation()` — copy from `app.cpp::update_node_content_from_simulation()`, replacing `scene.nodes()` with `scene().nodes()`, `simulation.get_port_value()` with `simulation_.get_port_value()`.
- `resetNodeContent(registry)` — copy from `app.cpp::reset_node_content()`, pass registry as parameter instead of member.
- `triggerSwitch()`, `holdButtonPress()`, `holdButtonRelease()` — copy from `app.cpp`, adjust member access.
- `addComponent()`, `addBlueprint()` — copy from `app.cpp`, take `TypeRegistry&` as parameter.
- `openSubWindow()` — copy from `app.cpp`.
- `applyInputResult()` — similar to `EditorApp::apply_input_result()` but returns action struct instead of setting member bools.

**Important adaptation notes:**

1. `EditorApp::apply_input_result()` directly sets `show_context_menu`, `show_node_context_menu` etc. as bool members. In `Document`, return these as a struct so the caller (main loop) can handle ImGui popups.
2. `EditorApp` accesses `scene` (root scene) directly as a member reference. `Document` uses `scene()` method returning `window_manager_.root().scene`.
3. `resetNodeContent()` needs `TypeRegistry&` passed in since Document doesn't own it.

---

### Phase 3: Create WindowSystem Class

**Files to create:**

- `src/editor/window_system.h`
- `src/editor/window_system.cpp`

```cpp
// src/editor/window_system.h
#pragma once

#include "document.h"
#include "visual/inspector/inspector.h"
#include "window/properties_window.h"
#include "json_parser/json_parser.h"
#include <memory>
#include <vector>
#include <string>

/// Manages all open documents and global panels.
/// Replaces EditorApp as the top-level controller.
class WindowSystem {
public:
    WindowSystem();

    // ── Document lifecycle ──

    Document& createDocument();
    Document* openDocument(const std::string& path);
    bool closeDocument(Document& doc);  // returns false if cancelled
    bool closeAllDocuments();

    // ── Active document ──

    Document* activeDocument() { return active_document_; }
    const Document* activeDocument() const { return active_document_; }
    void setActiveDocument(Document* doc);

    // ── Document access ──

    const std::vector<std::unique_ptr<Document>>& documents() const { return documents_; }
    size_t documentCount() const { return documents_.size(); }
    Document* findDocumentByPath(const std::string& path);

    // ── Global panels ──

    Inspector& inspector() { return inspector_; }
    PropertiesWindow& propertiesWindow() { return properties_window_; }
    TypeRegistry& typeRegistry() { return type_registry_; }

    // ── Context menu state (with source document) ──

    struct ContextMenuState {
        bool show = false;
        Pt position;
        std::string group_id;
        Document* source_doc = nullptr;
    } contextMenu;

    struct NodeContextMenuState {
        bool show = false;
        size_t node_index = 0;
        std::string group_id;
        Document* source_doc = nullptr;
    } nodeContextMenu;

    struct ColorPickerState {
        bool show = false;
        size_t node_index = 0;
        std::string group_id;
        Document* source_doc = nullptr;
        float rgba[4] = {0.5f, 0.5f, 0.5f, 1.0f};
    } colorPicker;

    bool showInspector = true;

    // ── Utility ──

    /// Remove documents that were marked closed. Call at end of frame.
    void removeClosedDocuments();

    /// Open properties for a node in the active document
    void openPropertiesForNode(size_t node_index, Document& doc);

    /// Open color picker for a node
    void openColorPickerForNode(size_t node_index, const std::string& group_id, Document& doc);

    /// Dispatch InputResultAction from a document to the window system
    void handleInputAction(const Document::InputResultAction& action, Document& doc);

private:
    std::vector<std::unique_ptr<Document>> documents_;
    Document* active_document_ = nullptr;
    TypeRegistry type_registry_;
    Inspector inspector_;
    PropertiesWindow properties_window_;
};
```

**Implementation** (`window_system.cpp`):

```cpp
WindowSystem::WindowSystem()
    : type_registry_(load_type_registry())
    , inspector_(/* needs a scene — use a placeholder, will be set by setActiveDocument */)
{
    // Create initial empty document
    createDocument();
}
```

**Problem**: `Inspector` constructor requires `const VisualScene&`. We need to handle the case where there's no document yet. This is why Phase 1 changed Inspector to use a pointer — construct with first document's scene, then switch via `setScene()`.

**Revised approach**: After Phase 1 changes Inspector to pointer-based, construct Inspector with nullptr or the first document's scene:

```cpp
WindowSystem::WindowSystem()
    : type_registry_(load_type_registry())
    , inspector_()  // default constructor with nullptr scene
{
    auto& doc = createDocument();
    inspector_.setScene(doc.scene());
}
```

This means `Inspector` also needs a default constructor. Add one:

```cpp
Inspector() : scene_(nullptr) {}
```

Key methods:

- `createDocument()`: Create new Document, push to vector, set as active
- `openDocument(path)`: Check if already open (by path), if so just activate. Else create new + load.
- `closeDocument(doc)`: If modified, prompt save (via return value — caller shows dialog). Remove from vector.
- `setActiveDocument(doc)`: Update `active_document_`, call `inspector_.setScene(doc->scene())`, `inspector_.markDirty()`
- `handleInputAction()`: Copy fields from `InputResultAction` into contextMenu/nodeContextMenu state, setting `source_doc`.

---

### Phase 4: Check ImGui Docking Availability

Before rewriting the main loop, verify if ImGui docking is available.

**Check**: Look at the imgui source in `_deps/imgui-src/`:

```bash
grep -r "DockSpace\|IMGUI_HAS_DOCK" _deps/imgui-src/imgui.h | head -5
```

**If docking IS available**: Use `ImGui::DockSpace()` for document tabs in the center area.
**If docking is NOT available**: Use manual `ImGui::BeginTabBar()` / `ImGui::BeginTabItem()` for document tabs.

**Manual tab approach (fallback)**:

```cpp
ImGui::BeginTabBar("DocumentTabs");
for (auto& doc : ws.documents()) {
    ImGuiTabItemFlags flags = ImGuiTabItemFlags_None;
    if (doc->isModified()) flags |= ImGuiTabItemFlags_UnsavedDocument;

    bool open = true;
    if (ImGui::BeginTabItem(doc->title().c_str(), &open, flags)) {
        // This tab is selected = active document
        ws.setActiveDocument(doc.get());
        // Render canvas inside this tab
        render_document_canvas(*doc, ws);
        ImGui::EndTabItem();
    }
    if (!open) {
        ws.closeDocument(*doc);
    }
}
ImGui::EndTabBar();
```

**With docking**: Each document is its own ImGui window with `ImGui::Begin()`, and ImGui handles docking/tabbing automatically. Prefer this if available.

---

### Phase 5: Rewrite Main Loop (an24_editor.cpp)

This is the largest change. The current `an24_editor.cpp` is ~750 lines with a monolithic main loop. Refactor to use `WindowSystem`.

**Key changes:**

5.1. Replace `EditorApp app;` with `WindowSystem ws;`

5.2. Extract `process_window` into a standalone function (NOT a lambda capturing `app`):

```cpp
/// Render and handle input for a BlueprintWindow.
/// This is the core per-window render + input handler.
void processWindow(BlueprintWindow& win, Document& doc, WindowSystem& ws,
                   Pt cmin, Pt cmax, ImDrawList* draw_list, bool hovered)
{
    ImGuiIO& io = ImGui::GetIO();
    ImGuiDrawList dl;
    dl.dl = draw_list;

    // 1. Update hover
    if (hovered) {
        ImVec2 mp = ImGui::GetMousePos();
        Pt mouse_world = win.scene.viewport().screen_to_world(Pt(mp.x, mp.y), cmin);
        win.input.update_hover(mouse_world);
    } else {
        win.input.update_hover(Pt(-1e9f, -1e9f));
    }

    // 2. Render grid + scene
    BlueprintRenderer::renderGrid(dl, win.scene.viewport(), cmin, cmax);
    win.scene.render(dl, cmin, cmax,
                     &win.input.selected_nodes(), win.input.selected_wire(),
                     &doc.simulation(), win.input.hovered_wire());

    // 3. Tooltip
    if (hovered) {
        ImVec2 mp = ImGui::GetMousePos();
        Pt world = win.scene.viewport().screen_to_world(Pt(mp.x, mp.y), cmin);
        auto tooltip = win.scene.detectTooltip(world, doc.simulation(), cmin);
        BlueprintRenderer::renderTooltip(dl, tooltip);
    }

    // 4. Temp wire
    if (win.input.has_temp_wire()) {
        // ... same as current code ...
    }

    // 5. Node content widgets (ImGui overlays)
    for (auto& node : doc.blueprint().nodes) {
        if (node.group_id != win.scene.groupId()) continue;
        // ... same as current code, but use doc.holdButtonPress() etc ...
    }

    // 6. Marquee selection
    // ... same as current code ...

    // 7. Input handling
    if (!hovered) return;
    // ... same as current code, but dispatch via:
    auto action = doc.applyInputResult(win.input.on_mouse_down(...), win.group_id);
    ws.handleInputAction(action, doc);
}
```

5.3. Main loop structure:

```cpp
int main(int argc, char** argv) {
    // ... SDL/OpenGL/ImGui setup (KEEP exactly as-is) ...

    WindowSystem ws;

    // Load file from CLI or default
    if (argc > 1) {
        ws.openDocument(argv[1]);
    } else {
        ws.activeDocument()->load("/Users/vladimir/an24_cpp/blueprint.json");
        // or just start with empty
    }

    bool running = true;
    while (running) {
        // ... SDL event polling (KEEP as-is) ...
        // ... ImGui NewFrame (KEEP as-is) ...

        // ════════════════════════════════════════════
        // Global keyboard shortcuts
        // ════════════════════════════════════════════
        if (!io.WantCaptureKeyboard) {
            Document* doc = ws.activeDocument();
            if (doc) {
                if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
                    if (doc->isSimulationRunning()) doc->stopSimulation();
                    else doc->startSimulation();
                }
            }
            // Ctrl+N → new document
            if ((io.KeyCtrl || io.KeySuper) && ImGui::IsKeyPressed(ImGuiKey_N)) {
                ws.createDocument();
            }
            // Ctrl+O → open
            if ((io.KeyCtrl || io.KeySuper) && ImGui::IsKeyPressed(ImGuiKey_O)) {
                nfdu8filteritem_t filter = {"Blueprint JSON", "json"};
                nfdchar_t* outPath = nullptr;
                if (NFD_OpenDialog(&outPath, &filter, 1, nullptr) == NFD_OKAY) {
                    ws.openDocument(outPath);
                    NFD_FreePath(outPath);
                }
            }
            // Ctrl+S → save
            if ((io.KeyCtrl || io.KeySuper) && ImGui::IsKeyPressed(ImGuiKey_S)) {
                if (doc && doc->isModified()) {
                    if (doc->filepath().empty()) {
                        // Save As dialog
                        nfdu8filteritem_t filter = {"Blueprint JSON", "json"};
                        nfdchar_t* outPath = nullptr;
                        if (NFD_SaveDialog(&outPath, &filter, 1, nullptr, "blueprint.json") == NFD_OKAY) {
                            doc->save(outPath);
                            NFD_FreePath(outPath);
                        }
                    } else {
                        doc->save(doc->filepath());
                    }
                }
            }
        }

        // ════════════════════════════════════════════
        // Simulation update (active document only)
        // ════════════════════════════════════════════
        if (Document* doc = ws.activeDocument()) {
            doc->updateSimulationStep(io.DeltaTime);
            doc->updateNodeContentFromSimulation();
        }

        // ════════════════════════════════════════════
        // Menu bar
        // ════════════════════════════════════════════
        if (ImGui::BeginMainMenuBar()) {
            Document* doc = ws.activeDocument();

            // Simulation indicator
            if (doc && doc->isSimulationRunning()) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1,1,0,1), "▶ SIM");
            }

            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New", "Ctrl+N")) ws.createDocument();
                if (ImGui::MenuItem("Open...", "Ctrl+O")) { /* NFD dialog */ }
                if (ImGui::MenuItem("Save", "Ctrl+S", false, doc && doc->isModified())) { /* save logic */ }
                ImGui::Separator();
                if (ImGui::MenuItem("Close Tab", nullptr, false, ws.documentCount() > 1)) {
                    if (doc) ws.closeDocument(*doc);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit", "Alt+F4")) running = false;
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                if (ImGui::MenuItem("Inspector", nullptr, ws.showInspector)) {
                    ws.showInspector = !ws.showInspector;
                }
                ImGui::Separator();
                if (doc) {
                    if (ImGui::MenuItem("Zoom In", "Ctrl++")) {
                        doc->scene().viewport().zoom *= 1.1f;
                        doc->scene().viewport().clamp_zoom();
                    }
                    if (ImGui::MenuItem("Zoom Out", "Ctrl+-")) {
                        doc->scene().viewport().zoom /= 1.1f;
                        doc->scene().viewport().clamp_zoom();
                    }
                    if (ImGui::MenuItem("Reset Zoom", "Ctrl+0")) {
                        doc->scene().viewport().zoom = 1.0f;
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit")) {
                bool has_sel = doc && !doc->input().selected_nodes().empty();
                if (ImGui::MenuItem("Delete", "Del", false, has_sel)) {
                    auto action = doc->applyInputResult(doc->input().on_key(Key::Delete));
                    ws.handleInputAction(action, *doc);
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // ════════════════════════════════════════════
        // Inspector panel (left, docked)
        // ════════════════════════════════════════════
        // Keep existing manual layout with splitter
        // But render inspector only if activeDocument exists
        if (ws.showInspector) {
            // ... same layout code as now ...
            ImGui::Begin("Inspector", &ws.showInspector, /* same flags */);
            ws.inspector().render();
            std::string sel = ws.inspector().consumeSelection();
            if (!sel.empty() && ws.activeDocument()) {
                ws.activeDocument()->input().selectNodeById(sel);
            }
            ImGui::End();
            // ... same splitter code ...
        }

        // ════════════════════════════════════════════
        // Document tabs + canvas
        // ════════════════════════════════════════════
        // Option A: ImGui::BeginTabBar (if no docking)
        // Option B: ImGui::DockSpace (if docking available)

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::SetNextWindowPos(ImVec2(canvas_x, menu_height));
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x - canvas_x, available_h));
        ImGui::Begin("##DocumentArea", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);

        if (ImGui::BeginTabBar("DocumentTabs")) {
            for (auto& doc : ws.documents()) {
                ImGuiTabItemFlags flags = ImGuiTabItemFlags_None;
                if (doc->isModified()) flags |= ImGuiTabItemFlags_UnsavedDocument;

                bool tab_open = true;
                std::string tab_label = doc->title() + "###" + doc->id();
                if (ImGui::BeginTabItem(tab_label.c_str(), &tab_open, flags)) {
                    // This tab is selected → active document
                    if (ws.activeDocument() != doc.get()) {
                        ws.setActiveDocument(doc.get());
                    }

                    // Render canvas for this document's root window
                    auto canvas_min = ImGui::GetWindowContentRegionMin();
                    auto canvas_max = ImGui::GetWindowContentRegionMax();
                    Pt cmin(canvas_min.x + ImGui::GetWindowPos().x,
                            canvas_min.y + ImGui::GetWindowPos().y);
                    Pt cmax(canvas_max.x + ImGui::GetWindowPos().x,
                            canvas_max.y + ImGui::GetWindowPos().y);
                    bool hovered = ImGui::IsWindowHovered();

                    processWindow(doc->root(), *doc, ws, cmin, cmax,
                                  ImGui::GetWindowDrawList(), hovered);

                    ImGui::EndTabItem();
                }
                if (!tab_open) {
                    // User clicked X on tab — close document
                    ws.closeDocument(*doc);
                }
            }
            ImGui::EndTabBar();
        }

        ImGui::End();
        ImGui::PopStyleVar();

        // ════════════════════════════════════════════
        // Sub-blueprint windows (per document)
        // ════════════════════════════════════════════
        for (auto& doc : ws.documents()) {
            doc->windowManager().removeClosedWindows();
            for (auto& win_ptr : doc->windowManager().windows()) {
                auto& win = *win_ptr;
                if (win.group_id.empty()) continue;
                if (!win.open) continue;

                ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
                // Include doc ID in window ID to prevent conflicts between documents
                std::string win_title = win.title + " [" + doc->displayName() + "]###"
                                       + doc->id() + ":" + win.group_id;
                if (!ImGui::Begin(win_title.c_str(), &win.open,
                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
                    ImGui::End();
                    continue;
                }

                // Toolbar (Fit View, Auto Layout, Delete) — same as current code
                // but use doc->blueprint() instead of app.blueprint
                // ...

                ImVec2 content_size = ImGui::GetContentRegionAvail();
                ImGui::InvisibleButton(("##canvas_" + doc->id() + "_" + win.group_id).c_str(), content_size);
                bool sub_hovered = ImGui::IsItemHovered();

                auto sub_cmin = ImGui::GetWindowContentRegionMin();
                auto sub_cmax = ImGui::GetWindowContentRegionMax();
                Pt sub_min(sub_cmin.x + ImGui::GetWindowPos().x, sub_cmin.y + ImGui::GetWindowPos().y);
                Pt sub_max(sub_cmax.x + ImGui::GetWindowPos().x, sub_cmax.y + ImGui::GetWindowPos().y);

                processWindow(win, *doc, ws, sub_min, sub_max, ImGui::GetWindowDrawList(), sub_hovered);

                ImGui::End();
            }
        }

        // ════════════════════════════════════════════
        // Context menus + Color picker + Properties
        // ════════════════════════════════════════════
        // Same logic as current code, but use ws.contextMenu.source_doc
        // instead of app.blueprint directly:
        if (ws.contextMenu.show) {
            ImGui::OpenPopup("AddComponent");
            ws.contextMenu.show = false;
        }
        if (ImGui::BeginPopup("AddComponent")) {
            // ... same menu tree code ...
            // But add_component goes to the source document:
            // ws.contextMenu.source_doc->addComponent(classname, pos, group_id, ws.typeRegistry());
            ImGui::EndPopup();
        }

        // Node context menu, color picker, properties — similar adaptation
        // ...

        ws.propertiesWindow().render();

        // ════════════════════════════════════════════
        // Cleanup: remove closed documents
        // ════════════════════════════════════════════
        ws.removeClosedDocuments();

        // Render
        ImGui::Render();
        // ... GL swap (KEEP as-is) ...
    }
    // ... cleanup (KEEP as-is) ...
}
```

---

### Phase 6: Update CMakeLists.txt and Build

Add new source files to `examples/CMakeLists.txt`:

```cmake
add_executable(an24_editor
    ${CMAKE_SOURCE_DIR}/examples/an24_editor.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/app.cpp           # KEEP for now (can remove later)
    ${CMAKE_SOURCE_DIR}/src/editor/document.cpp       # NEW
    ${CMAKE_SOURCE_DIR}/src/editor/window_system.cpp  # NEW
    # ... all existing sources stay ...
)
```

Build and fix compilation errors.

Once the new code compiles and works, `app.h` / `app.cpp` can be deleted (and removed from CMakeLists.txt). But keep them during development for reference.

---

### Phase 7: Testing and Polish

7.1. **Verify existing functionality works** with a single document (regression test):

- Open a blueprint file → renders correctly
- Add components via context menu
- Connect wires
- Start/stop simulation
- Open sub-blueprint windows
- Inspector shows correct tree
- Properties window works
- Color picker works
- Save/load works

  7.2. **Test MDI features**:

- Ctrl+N creates new tab
- Ctrl+O opens file in new tab
- Clicking tabs switches active document
- Inspector updates when switching tabs
- Simulation runs only on active document
- Modified indicator (\*) appears on edit
- Closing tab with modified doc prompts save
- Sub-windows belong to correct document
- Context menu adds to correct document

  7.3. **Edge cases**:

- Close last tab (should create new untitled)
- Open same file twice (should switch to existing tab)
- Multiple documents with sub-windows open simultaneously

---

## 📁 Summary of New/Modified Files

### New files to create:

| File                           | Purpose                                                                     |
| ------------------------------ | --------------------------------------------------------------------------- |
| `src/editor/imgui_draw_list.h` | `ImGuiDrawList : public IDrawList` adapter (extracted from an24_editor.cpp) |
| `src/editor/document.h`        | `Document` class header                                                     |
| `src/editor/document.cpp`      | `Document` implementation (methods from app.cpp)                            |
| `src/editor/window_system.h`   | `WindowSystem` class header                                                 |
| `src/editor/window_system.cpp` | `WindowSystem` implementation                                               |

### Files to modify:

| File                                               | Changes                                                                                |
| -------------------------------------------------- | -------------------------------------------------------------------------------------- |
| `examples/an24_editor.cpp`                         | Major rewrite: replace EditorApp with WindowSystem, add tab bar, extract processWindow |
| `examples/CMakeLists.txt`                          | Add document.cpp, window_system.cpp                                                    |
| `src/editor/visual/inspector/inspector.h`          | Change `scene_` from ref to pointer, add `setScene()`, add default constructor         |
| `src/editor/visual/inspector/inspector_core.cpp`   | Update `scene_.` → `scene_->`                                                          |
| `src/editor/visual/inspector/inspector_render.cpp` | Update `scene_.` → `scene_->`                                                          |

### Files to keep unchanged:

| File                                         | Reason                                             |
| -------------------------------------------- | -------------------------------------------------- |
| `src/editor/data/*`                          | Data model is correct as-is                        |
| `src/editor/visual/scene/scene.h`            | VisualScene is per-window, no changes needed       |
| `src/editor/visual/scene/persist.h/.cpp`     | Persistence layer stays                            |
| `src/editor/window/window_manager.h`         | WindowManager is per-document, no changes          |
| `src/editor/window/blueprint_window.h`       | BlueprintWindow is fine                            |
| `src/editor/window/properties_window.h/.cpp` | PropertiesWindow stays in place                    |
| `src/editor/input/*`                         | Input handling unchanged                           |
| `src/editor/visual/node/*`                   | Visual nodes unchanged                             |
| `src/editor/visual/renderer/*`               | Renderers unchanged                                |
| `src/editor/viewport/*`                      | Viewport unchanged                                 |
| `src/editor/router/*`                        | Router unchanged                                   |
| `src/editor/simulation.h/.cpp`               | SimulationController for tests, not used in editor |
| `src/jit_solver/*`                           | JIT solver unchanged                               |

### Files to delete (after migration verified):

| File                 | Replaced by                          |
| -------------------- | ------------------------------------ |
| `src/editor/app.h`   | `document.h` + `window_system.h`     |
| `src/editor/app.cpp` | `document.cpp` + `window_system.cpp` |

---

## 🔑 Design Decisions and Rationale

### 1. No IWindow/PanelWindow abstract hierarchy

**Why**: The original spec proposed `IWindow → Window → PanelWindow → InspectorWindow` etc. This is over-engineered for 3 window types. The current codebase uses simple structs and classes, not deep hierarchies. Adding 4 abstract classes for 3 concrete windows violates YAGNI and adds complexity without benefit right now.

**Instead**: `Document` is a plain class. `Inspector`, `PropertiesWindow` stay as they are. Window rendering stays in the main loop (as it depends on ImGui context).

### 2. No singleton WindowSystem

**Why**: Singletons are hard to test and create hidden dependencies. Current `EditorApp` is a stack object.

**Instead**: `WindowSystem ws;` is a stack object in `main()`, passed by reference where needed.

### 3. processWindow as free function, not a method

**Why**: `processWindow` needs `ImGui::GetMousePos()`, `ImGui::IsMouseDragging()`, `ImGui::GetWindowDrawList()` — all ImGui globals that only make sense during the render loop. Putting this inside `Document` or a window class would create a hidden dependency on ImGui render context.

**Instead**: `processWindow()` is a free function in `an24_editor.cpp` (or a separately included file), called from the main loop.

### 4. Document does NOT own Inspector or PropertiesWindow

**Why**: Inspector and PropertiesWindow are global panels that switch between documents. They're NOT per-document.

**Instead**: `WindowSystem` owns one `Inspector` and one `PropertiesWindow`, which point to the active document's data.

### 5. InputResultAction return value instead of member bools

**Why**: `EditorApp` used `bool show_context_menu` members set by `apply_input_result()`. In MDI, context menus need to know WHICH document they apply to. Returning a struct allows the caller (main loop) to route to the correct document.

### 6. No BlueprintPersistence abstraction layer

**Why**: The spec proposed `BlueprintPersistence::serialize()` for future blueprint references. But `save_blueprint_to_file()` and `load_blueprint_from_file()` already exist as the persistence API. Adding another layer is premature.

**Instead**: `Document::save()` uses `save_blueprint_to_file()` directly. When blueprint references are added, the change will be in `persist.cpp`, not in `Document`.

---

## ⚠️ Gotchas and Pitfalls

1. **WindowManager cannot be moved**. It holds `BlueprintWindow` objects which contain references to the shared `Blueprint`. `Document` must NOT be movable. Use `unique_ptr<Document>` in the vector.

2. **ImGui window IDs must be unique across documents**. Use `"###" + doc.id()` suffix for ImGui stable IDs. Sub-windows need `doc.id() + ":" + group_id`.

3. **Inspector scene pointer**: After changing from reference to pointer, all `scene_.` must become `scene_->` in inspector source files. Null check needed in `render()`: if `!scene_` return early.

4. **Tab close during iteration**: When closing a tab, don't erase from `documents_` vector during rendering loop. Mark for removal, handle in `removeClosedDocuments()` at end of frame.

5. **Active document can be null**: After closing the last document, `activeDocument()` returns nullptr. All code accessing active doc must null-check.

6. **Multiple sub-windows with same group_id**: Different documents could have the same group_id (e.g., both have a "lamp1" sub-blueprint). ImGui window IDs must include the document ID to prevent conflicts.

7. **Signal overrides + held buttons**: These are per-document. When switching active document, ongoing button holds on the previous document should release (or persist — design choice).

8. **File dialog blocking**: NFD dialogs are modal/blocking. During the dialog, ImGui doesn't render. This is fine for now.

---

## 📐 Coding Style Reference

Match these patterns from the existing codebase:

```cpp
// Headers: #pragma once, includes, then class
#pragma once
#include "data/blueprint.h"
#include <string>
#include <vector>

// Classes: descriptive doc comment, then class
/// Document - represents a single open blueprint file with its simulation state.
class Document {
public:
    // Group related methods with comments
    // ── Identity ──
    const std::string& id() const { return id_; }

private:
    std::string id_;
};
```

- One-line getters/setters inline in header
- Multi-line methods in .cpp
- `/// Doxygen` for public API, `//` for implementation
- Include guards: `#pragma once`
- Forward declarations preferred over includes in headers
- `const` correctness: `const` for getters, `const` ref for string params
- No `using namespace` in headers
- `assert(bp_)` for invariants (see VisualScene)
