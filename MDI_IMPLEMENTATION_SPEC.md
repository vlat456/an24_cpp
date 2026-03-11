# MDI Editor Implementation Specification

**Project**: AN-24 Blueprint Editor - SDI to MDI Migration
**Date**: 2026-03-11
**Status**: Ready for Implementation
**Complexity**: High (architectural refactoring)

---

## 📋 Executive Summary

**Goal**: Convert single-document editor (SDI) to multi-document interface (MDI) with ImGui docking support.

**Key Changes**:
- Each document has its own `Blueprint` + `Simulator`
- Multiple documents open simultaneously as dockable tabs
- Per-window menu bar (NOT global menu bar)
- Global Inspector/Properties windows (auto-switch to active document)
- Modified indicator (`*`) in window title
- **OOP architecture**: Window hierarchy with base classes and inheritance

**Non-Goals**:
- Workspace management (not needed)
- Multiple simultaneous simulations (only active document simulates)
- Sub-blueprint windows as dockable (remain floating as-is)

---

## 🏗️ Architecture Overview

### Before (SDI)
```
EditorApp (singleton)
├── Blueprint (one)
├── Simulator (one)
├── WindowManager (manages sub-blueprint windows)
├── Inspector (global)
└── PropertiesWindow (global)
```

### After (MDI) - OOP Window Hierarchy
```
WindowSystem (singleton)
├── DocumentManager (manages DocumentWindow instances)
│   └── vector<unique_ptr<DocumentWindow>> documents
│       ├── DocumentWindow 1
│       │   ├── Blueprint
│       │   ├── Simulator
│       │   └── WindowManager (sub-blueprint windows)
│       └── DocumentWindow 2
└── PanelManager (manages global panels)
    ├── InspectorWindow (IWindow)
    └── PropertiesWindow (IWindow)
```

### OOP Window Class Hierarchy
```
                    IWindow (interface)
                       /              \
           DocumentWindow         PanelWindow
                 /                   \
       BlueprintPanelWindow    InspectorWindow
                                  PropertiesWindow
                                  (etc.)
```

---

## 🚨 FUTURE ARCHITECTURE NOTE: JSON Blueprint References

### Current State (This Implementation)
JSON files store a **flat list** of all nodes, including sub-blueprint internal nodes:
```json
{
  "nodes": [
    {"id": "battery1", "type": "Battery", ...},
    {"id": "lamp1:lamp", "type": "Lamp", ...},     // Sub-blueprint internal node
    {"id": "lamp1:switch", "type": "Switch", ...}  // Sub-blueprint internal node
  ],
  "collapsed_groups": [
    {"id": "lamp1", "type_name": "lamp_pass_through", "internal_node_ids": ["lamp1:lamp", "lamp1:switch"]}
  ]
}
```

### Future State (NOT in this implementation)
JSON files will store **references** to other blueprint JSON files:
```json
{
  "nodes": [
    {"id": "battery1", "type": "Battery", ...}
  ],
  "blueprint_refs": [
    {
      "id": "lamp1",
      "type": "lamp_pass_through",
      "path": "library/lamp_pass_through.json",  // ← Reference to external JSON
      "integrated": false  // ← If true, stores flat list instead
    }
  ]
}
```

### Flatten During Load
When loading a blueprint with references:
1. Parser reads `blueprint_refs`
2. For each ref with `integrated: false`:
   - Load external JSON file
   - Prefix all node IDs with ref ID
   - Merge into parent blueprint
3. Result is **same in-memory structure** as current flat list

### "Integrate Blueprint Permanently" Feature
Future UI command to convert a reference to integrated (flat):
```cpp
// User right-clicks on collapsed blueprint node → "Integrate Permanently"
void DocumentWindow::integrate_blueprint(const std::string& group_id) {
    // Find the blueprint_ref
    // Load external JSON
    // Merge nodes into parent blueprint
    // Set blueprint_ref.integrated = true
    // Save modified blueprint
    mark_modified();
}
```

### ⚠️ Why This Matters for Current Implementation

**DO NOT hardcode assumptions about flat-list storage** in:
- `Document::save()` - Keep blueprint serialization separate
- `Document::load()` - Keep blueprint deserialization separate
- `Blueprint::serialize_to_json()` - Prepare for `blueprint_refs` field
- `Blueprint::deserialize_from_json()` - Prepare to handle refs

**Recommended abstraction**:
```cpp
// src/editor/data/blueprint_persistence.h
class BlueprintPersistence {
public:
    // Serialize to JSON (handles both flat and ref-based)
    static nlohmann::json serialize(const Blueprint& bp);

    // Deserialize from JSON (auto-flattens refs)
    static Blueprint deserialize(const nlohmann::json& j);

    // Future: Integrate a blueprint ref into flat list
    static void integrate_ref(Blueprint& bp, const std::string& ref_id);
};
```

**Benefit**: When we add reference-based blueprints, we only change `BlueprintPersistence`, not `DocumentWindow` or `WindowSystem`.

### Implementation Guidance for This Task

When implementing `Document::save()` and `Document::load()`:
```cpp
// ✅ GOOD - Use persistence layer
bool Document::save(const std::string& path) {
    nlohmann::json j = BlueprintPersistence::serialize(blueprint_);
    // ... write to file ...
}

bool Document::load(const std::string& path) {
    // ... read from file ...
    blueprint_ = BlueprintPersistence::deserialize(j);
    // ...
}
```

```cpp
// ❌ BAD - Direct JSON manipulation (future-proofing)
bool Document::save(const std::string& path) {
    nlohmann::json j;
    j["nodes"] = blueprint_.nodes;  // ← Assumes flat list
    // This will break when we add blueprint_refs!
}
```

---

## 📦 New Classes to Create

### 1. `IWindow` Interface

**File**: `src/editor/visual/windows/window.h`

```cpp
#pragma once

#include <string>

/// Base interface for all windows in the editor
/// All windows (documents, panels, dialogs) implement this interface
class IWindow {
public:
    virtual ~IWindow() = default;

    /// Get window title for ImGui
    virtual std::string get_title() const = 0;

    /// Get unique window ID (for ImGui: "Title###ID")
    virtual std::string get_window_id() const = 0;

    /// Render the window content
    /// Called every frame when window is open
    virtual void render() = 0;

    /// Check if window is currently open
    virtual bool is_open() const = 0;

    /// Close the window
    virtual void close() = 0;

    /// Check if window can be closed (e.g., prompt for save)
    virtual bool can_close() const { return true; }

    /// Get window type name (for debugging/reflection)
    virtual const char* get_type_name() const = 0;

    /// Optional: Get preferred dock position
    virtual std::string get_dock_id() const { return ""; }

    /// Optional: Menu rendering for document windows
    virtual void render_menu_bar() {}

protected:
    IWindow() = default;
};
```

---

### 2. `Window` Base Class

**File**: `src/editor/visual/windows/window.h` (continued)

```cpp
#pragma once

#include "iwindow.h"
#include <string>

/// Base implementation of IWindow with common functionality
class Window : public IWindow {
public:
    // ========================================================================
    // IWindow implementation
    // ========================================================================

    std::string get_window_id() const override {
        return get_title() + "###" + get_id();
    }

    bool is_open() const override { return open_; }
    void close() override { open_ = false; }

    const char* get_type_name() const override {
        return get_type_name_impl();
    }

    std::string get_dock_id() const override {
        return dock_id_;
    }

    void set_dock_id(const std::string& id) {
        dock_id_ = id;
    }

    // ========================================================================
    // Window state
    // ========================================================================

    /// Set window open/closed
    void set_open(bool value) { open_ = value; }

protected:
    // ========================================================================
    // Derived classes must implement
    // ========================================================================

    /// Get unique ID (override in derived class)
    virtual std::string get_id() const = 0;

    /// Get type name (override in derived class)
    virtual const char* get_type_name_impl() const = 0;

    // ========================================================================
    // Protected state
    // ========================================================================

    Window() = default;
    explicit Window(const std::string& id) : id_(id) {}

    /// Window open state
    bool open_ = true;

    /// Unique identifier
    std::string id_;

    /// Preferred dock position ID
    std::string dock_id_;
};
```

---

### 3. `PanelWindow` Base Class

**File**: `src/editor/visual/windows/panel_window.h`

```cpp
#pragma once

#include "window.h"

/// Base class for all panel windows (Inspector, Properties, etc.)
/// Panel windows are global, singleton-like, and show data from active document
class PanelWindow : public Window {
public:
    // ========================================================================
    // Render wrapper (handles docking and window frame)
    // ========================================================================

    void render() override final {
        if (!open_) return;

        // Apply dock position if set
        if (!dock_id_.empty()) {
            ImGui::SetNextWindowDockID(
                ImGui::GetID(dock_id_.c_str()),
                ImGuiCond_FirstUseEver
            );
        }

        // Render window with menu bar (if implemented)
        ImGuiWindowFlags flags = get_window_flags();

        if (ImGui::Begin(get_window_id().c_str(), &open_, flags)) {
            if (has_menu_bar()) {
                if (ImGui::BeginMenuBar()) {
                    render_menu_bar();
                    ImGui::EndMenuBar();
                }
            }

            // Derived class renders content
            render_content();
        }

        ImGui::End();
    }

    // ========================================================================
    // Derived classes override
    // ========================================================================

    /// Render the panel content (called between Begin/End)
    virtual void render_content() = 0;

    /// Get ImGui window flags (override in derived if needed)
    virtual ImGuiWindowFlags get_window_flags() const {
        return ImGuiWindowFlags_None;
    }

    /// Does this panel have a menu bar?
    virtual bool has_menu_bar() const { return false; }

protected:
    PanelWindow() = default;
    explicit PanelWindow(const std::string& id) : Window(id) {}
};
```

---

### 4. `DocumentWindow` Class

**File**: `src/editor/visual/windows/document_window.h`

> **⚠️ FUTURE-PROOFING NOTE**: When implementing `save()` and `load()` methods,
> use Blueprint's serialization methods rather than directly accessing `blueprint_.nodes`.
> See "FUTURE ARCHITECTURE NOTE: JSON Blueprint References" section above for details.
> This ensures compatibility when we add blueprint reference support.

```cpp
#pragma once

#include "window.h"
#include "../../data/blueprint.h"
#include "../window/window_manager.h"
#include "../../../jit_solver/simulator.h"
#include <functional>
#include <unordered_map>
#include <unordered_set>

/// Represents a document window containing a blueprint + simulation
/// This is the main MDI document window with menu bar and canvas
class DocumentWindow : public Window {
public:
    // ========================================================================
    // Construction
    // ========================================================================

    /// Create a new untitled document
    DocumentWindow();

    /// Create a document from a file
    explicit DocumentWindow(const std::string& filepath);

    // ========================================================================
    // IWindow implementation
    // ========================================================================

    std::string get_title() const override {
        return get_title_with_modified();
    }

    void render() override final;

    bool can_close() const override {
        return !modified_;
    }

    void render_menu_bar() override;

    // ========================================================================
    // Document state
    // ========================================================================

    /// File path (empty if unsaved)
    const std::string& get_filepath() const { return filepath_; }

    /// Display name (filename or "Untitled")
    const std::string& get_display_name() const { return display_name_; }

    /// Modified flag
    bool is_modified() const { return modified_; }
    void set_modified(bool value) { modified_ = value; }
    void mark_modified() { modified_ = true; }
    void clear_modified() { modified_ = false; }

    // ========================================================================
    // Blueprint & Simulation access
    // ========================================================================

    Blueprint& get_blueprint() { return blueprint_; }
    const Blueprint& get_blueprint() const { return blueprint_; }

    an24::Simulator<an24::JIT_Solver>& get_simulation() { return simulation_; }
    const an24::Simulator<an24::JIT_Solver>& get_simulation() const {
        return simulation_;
    }

    bool is_simulation_running() const { return simulation_running_; }

    WindowManager& get_window_manager() { return window_manager_; }
    const WindowManager& get_window_manager() const { return window_manager_; }

    // ========================================================================
    // Convenience accessors (for backward compatibility)
    // ========================================================================

    /// Root window (for quick access)
    BlueprintWindow& root() { return window_manager_.root(); }
    const BlueprintWindow& root() const { return window_manager_.root(); }

    /// Scene, input, wire_manager for root window
    VisualScene& scene() { return root().scene; }
    CanvasInput& input() { return root().input; }
    WireManager& wire_manager() { return root().wire_manager; }

    // ========================================================================
    // Simulation control
    // ========================================================================

    void start_simulation();
    void stop_simulation();
    void update_simulation(float dt);
    void rebuild_simulation();

    // ========================================================================
    // Node content sync
    // ========================================================================

    void update_node_content_from_simulation(an24::TypeRegistry& registry);
    void reset_node_content(an24::TypeRegistry& registry);

    // ========================================================================
    // Input handling
    // ========================================================================

    void trigger_switch(const std::string& node_id);
    void hold_button_press(const std::string& node_id);
    void hold_button_release(const std::string& node_id);

    void add_component(const std::string& classname, Pt world_pos,
                      const std::string& group_id = "");
    void add_blueprint(const std::string& blueprint_name, Pt world_pos,
                      const std::string& group_id = "");
    void open_sub_window(const std::string& collapsed_group_id);

    // ========================================================================
    // Properties
    // ========================================================================

    void open_properties_for_node(size_t node_index);
    void open_color_picker_for_node(size_t node_index);

    // ========================================================================
    // Signal overrides (for button clicks, etc.)
    // ========================================================================

    std::unordered_map<std::string, float>& signal_overrides() {
        return signal_overrides_;
    }

    std::unordered_set<std::string>& held_buttons() {
        return held_buttons_;
    }

    // ========================================================================
    // File I/O
    // ========================================================================

    bool save(const std::string& path);
    bool load(const std::string& path);

protected:
    // ========================================================================
    // Window implementation
    // ========================================================================

    std::string get_id() const override { return id_; }
    const char* get_type_name_impl() const override { return "DocumentWindow"; }

private:
    // ========================================================================
    // Helper methods
    // ========================================================================

    std::string get_title_with_modified() const;
    void render_canvas();

    // ========================================================================
    // Document data
    // ========================================================================

    /// Unique document ID
    std::string id_;

    /// File path (empty if untitled)
    std::string filepath_;

    /// Display name for UI
    std::string display_name_;

    /// Modified flag
    bool modified_ = false;

    /// Blueprint data
    Blueprint blueprint_;

    /// Simulation instance
    an24::Simulator<an24::JIT_Solver> simulation_;

    /// Simulation running state
    bool simulation_running_ = false;

    /// Window manager for sub-blueprint windows
    WindowManager window_manager_;

    /// Signal overrides
    std::unordered_map<std::string, float> signal_overrides_;

    /// HoldButtons being held
    std::unordered_set<std::string> held_buttons_;

    /// Type registry reference (set by WindowSystem)
    an24::TypeRegistry* type_registry_ = nullptr;
    friend class WindowSystem;
};
```

---

### 5. `InspectorWindow` Class

**File**: `src/editor/visual/windows/inspector_window.h`

```cpp
#pragma once

#include "panel_window.h"
#include "../inspector/inspector.h"

/// Inspector panel - shows component tree with port connections
/// This is a global panel that shows data from the active document
class InspectorWindow : public PanelWindow {
public:
    explicit InspectorWindow(const std::string& id = "inspector");

    // ========================================================================
    // PanelWindow implementation
    // ========================================================================

    void render_content() override;

    ImGuiWindowFlags get_window_flags() const override {
        return ImGuiWindowFlags_None;
    }

    // ========================================================================
    // Inspector access
    // ========================================================================

    Inspector& get_inspector() { return inspector_; }
    const Inspector& get_inspector() const { return inspector_; }

    /// Update inspector to show a different document's scene
    void set_document(DocumentWindow* doc);

    /// Mark inspector as dirty (rebuild display tree)
    void mark_dirty() {
        inspector_.markDirty();
    }

protected:
    std::string get_id() const override { return "inspector"; }
    const char* get_type_name_impl() const override { return "InspectorWindow"; }

private:
    Inspector inspector_;
    DocumentWindow* current_document_ = nullptr;
};
```

---

### 6. `PropertiesWindow` Class

**File**: `src/editor/visual/windows/properties_window.h`

```cpp
#pragma once

#include "panel_window.h"
#include "../../data/node.h"
#include <functional>

/// Properties modal dialog - edits node parameters
/// This is a global modal that works with the active document
class PropertiesWindow : public PanelWindow {
public:
    using PropertyCallback = std::function<void(const std::string& node_id)>;

    PropertiesWindow();

    // ========================================================================
    // PanelWindow implementation
    // ========================================================================

    void render_content() override;

    ImGuiWindowFlags get_window_flags() const override {
        return ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse;
    }

    // ========================================================================
    // Properties API
    // ========================================================================

    /// Open properties for a node
    void open(Node& node, PropertyCallback on_apply);

    /// Check if currently open
    bool is_open_for_node() const { return is_open(); }

    /// Get target node ID
    const std::string& get_target_node_id() const { return target_node_id_; }

protected:
    std::string get_id() const override { return "properties"; }
    const char* get_type_name_impl() const override { return "PropertiesWindow"; }

private:
    void apply_and_close();
    void cancel_and_close();

    // ========================================================================
    // State
    // ========================================================================

    Node* target_ = nullptr;
    std::string target_node_id_;
    PropertyCallback on_apply_;

    /// Snapshot for cancel/revert
    std::string snapshot_name_;
    std::unordered_map<std::string, std::string> snapshot_params_;
};
```

---

### 7. `WindowSystem` Class (Main Manager)

**File**: `src/editor/visual/windows/window_system.h`

```cpp
#pragma once

#include "document_window.h"
#include "inspector_window.h"
#include "properties_window.h"
#include <memory>
#include <vector>
#include <functional>

/// WindowSystem - manages all windows in the editor
/// This is the main singleton that replaces EditorApp
class WindowSystem {
public:
    // ========================================================================
    // Singleton access
    // ========================================================================

    static WindowSystem& instance();

    // Prevent copy/move
    WindowSystem(const WindowSystem&) = delete;
    WindowSystem& operator=(const WindowSystem&) = delete;

    // ========================================================================
    // Document lifecycle
    // ========================================================================

    /// Create a new untitled document
    /// Returns reference to the new document window
    DocumentWindow& create_document();

    /// Open a document from file
    /// If already open, switches to that document
    /// Returns pointer to document, or nullptr on error
    DocumentWindow* open_document(const std::string& path);

    /// Close a document
    /// Returns false if user cancelled
    bool close_document(DocumentWindow& doc);

    /// Close all documents (for application exit)
    bool close_all_documents();

    // ========================================================================
    // Active document
    // ========================================================================

    /// Get the active document (may be nullptr)
    DocumentWindow* active_document() { return active_document_; }
    const DocumentWindow* active_document() const { return active_document_; }

    /// Set the active document (called when user focuses a window)
    void set_active_document(DocumentWindow* doc);

    /// Get active document or throw
    DocumentWindow& require_active();
    const DocumentWindow& require_active() const;

    // ========================================================================
    // Window access
    // ========================================================================

    /// Get all document windows
    const std::vector<std::unique_ptr<DocumentWindow>>& documents() const {
        return documents_;
    }

    /// Get document by ID
    DocumentWindow* find_document_by_id(const std::string& id);

    /// Get document by filepath
    DocumentWindow* find_document_by_path(const std::string& path);

    /// Count of open documents
    size_t document_count() const { return documents_.size(); }

    // ========================================================================
    // Panel access
    // ========================================================================

    InspectorWindow& inspector() { return inspector_; }
    const InspectorWindow& inspector() const { return inspector_; }

    PropertiesWindow& properties() { return properties_; }
    const PropertiesWindow& properties() const { return properties_; }

    // ========================================================================
    // Global state
    // ========================================================================

    /// Type registry (shared across all documents)
    an24::TypeRegistry& type_registry() { return type_registry_; }

    /// Context menu state (tracks which document triggered it)
    struct ContextMenuState {
        bool show = false;
        Pt position;
        std::string group_id;
        DocumentWindow* source_doc = nullptr;
    } context_menu;

    /// Node context menu state
    struct NodeContextMenuState {
        bool show = false;
        size_t node_index = 0;
        std::string group_id;
        DocumentWindow* source_doc = nullptr;
    } node_context_menu;

    /// Color picker state
    struct ColorPickerState {
        bool show = false;
        size_t node_index = 0;
        std::string group_id;
        DocumentWindow* source_doc = nullptr;
        float rgba[4] = {0.5f, 0.5f, 0.5f, 1.0f};
    } color_picker;

    // ========================================================================
    // Rendering
    // ========================================================================

    /// Render all windows (call from main loop)
    void render();

    /// Remove closed documents (call at end of each frame)
    void remove_closed_documents();

private:
    WindowSystem();
    ~WindowSystem() = default;

    // ========================================================================
    // Internal state
    // ========================================================================

    /// All document windows
    std::vector<std::unique_ptr<DocumentWindow>> documents_;

    /// Currently active document
    DocumentWindow* active_document_ = nullptr;

    /// Document ID counter
    size_t next_document_id_ = 1;

    /// Type registry (loaded at startup)
    an24::TypeRegistry type_registry_;

    /// Panel windows
    InspectorWindow inspector_;
    PropertiesWindow properties_;

    // ========================================================================
    // Helpers
    // ========================================================================

    std::string generate_document_id();

    /// Prompt for save before close
    bool prompt_save(DocumentWindow& doc);
};
```

---

## 📁 File Structure

### New Directory Structure
```
src/editor/visual/windows/
├── iwindow.h                 (IWindow interface)
├── window.h                  (Window base class)
├── window.cpp                (Window implementation)
├── panel_window.h            (PanelWindow base class)
├── panel_window.cpp          (PanelWindow implementation)
├── document_window.h         (DocumentWindow class)
├── document_window.cpp       (DocumentWindow implementation)
├── inspector_window.h        (InspectorWindow class)
├── inspector_window.cpp      (InspectorWindow implementation)
├── properties_window.h       (PropertiesWindow class - move from window/)
├── properties_window.cpp     (PropertiesWindow implementation - move from window/)
├── window_system.h           (WindowSystem class)
└── window_system.cpp         (WindowSystem implementation)
```

### Files to DELETE
```
src/editor/app.h              (Replaced by WindowSystem)
src/editor/app.cpp            (Logic distributed to DocumentWindow)
src/editor/window/properties_window.h  (Moved to visual/windows/)
src/editor/window/properties_window.cpp (Moved to visual/windows/)
```

### Files to MODIFY
```
src/editor/CMakeLists.txt     (Add new source files)
examples/an24_editor.cpp      (Complete rewrite for WindowSystem)
```

### Files to KEEP (minimal changes)
```
src/editor/visual/inspector/inspector.h    (No changes - used by InspectorWindow)
src/editor/visual/scene/scene.h            (No changes)
src/editor/window/window_manager.h         (No changes)
src/editor/window/blueprint_window.h       (No changes)
src/editor/data/blueprint.h                (No changes)
src/jit_solver/simulator.h                 (No changes)
```

---

## 📝 Implementation Steps (ORDERED)

### Phase 1: Window Infrastructure (IWindow + Window + PanelWindow)
**Priority**: CRITICAL
**Estimated effort**: 2-3 hours

**🎯 TDD Approach**: Write failing unit tests for IWindow, Window, PanelWindow FIRST, then implement.

**Tasks**:

1.1. Create `src/editor/visual/windows/iwindow.h`
- Define IWindow interface with pure virtual methods

1.2. Create `src/editor/visual/windows/window.h` and `.cpp`
- Implement Window base class
- Implement get_window_id(), is_open(), close()

1.3. Create `src/editor/visual/windows/panel_window.h` and `.cpp`
- Implement PanelWindow base class
- Implement render() with docking support
- Implement render_content() pure virtual

**Test**:
```cpp
// Test that we can create and render a simple panel
class TestPanel : public PanelWindow {
    void render_content() override {
        ImGui::Text("Hello from TestPanel");
    }
    const char* get_type_name_impl() const override { return "TestPanel"; }
};

TestPanel panel;
panel.render();  // Should render "Hello from TestPanel"
```

---

### Phase 2: InspectorWindow (First Panel Implementation)
**Priority**: CRITICAL
**Estimated effort**: 2-3 hours

**🎯 TDD Approach**: Write failing tests for InspectorWindow BEFORE implementation.

**Tasks**:

2.1. Create `src/editor/visual/windows/inspector_window.h` and `.cpp`

```cpp
// inspector_window.cpp
InspectorWindow::InspectorWindow(const std::string& id)
    : PanelWindow(id)
    , inspector_(nullptr)  // Will be set via set_document()
{
    set_dock_id("LeftPanel");  // Dock to left by default
}

void InspectorWindow::render_content() {
    if (current_document_) {
        // Render inspector for current document
        ImGui::Text("%s", current_document_->get_title().c_str());
        ImGui::Separator();

        inspector_.render();

        // Handle click → select node
        std::string sel = inspector_.consumeSelection();
        if (!sel.empty()) {
            current_document_->input().selectNodeById(sel);
        }
    } else {
        ImGui::Text("No document");
    }
}

void InspectorWindow::set_document(DocumentWindow* doc) {
    if (doc != current_document_) {
        current_document_ = doc;
        if (doc) {
            inspector_.set_scene(doc->scene());
            inspector_.markDirty();
        }
    }
}
```

**Test**: Verify inspector renders and updates when document changes

---

### Phase 3: PropertiesWindow (Second Panel Implementation)
**Priority**: CRITICAL
**Estimated effort**: 1-2 hours

**Tasks**:

3.1. Move `src/editor/window/properties_window.h` → `src/editor/visual/windows/properties_window.h`

3.2. Update PropertiesWindow to inherit from PanelWindow

```cpp
// properties_window.h - modify to inherit from PanelWindow
class PropertiesWindow : public PanelWindow {
    // ... existing methods ...

    void render_content() override;  // Was render()

    // ... rest of existing implementation ...
};
```

3.3. Update properties_window.cpp to call render_content()

**Test**: Verify properties modal opens and edits node parameters

---

### Phase 4: DocumentWindow (Main Implementation)
**Priority**: CRITICAL
**Estimated effort**: 6-8 hours

**🎯 TDD Approach**: Write comprehensive failing tests for DocumentWindow BEFORE implementing each method group.

**Tasks**:

4.1. Create `src/editor/visual/windows/document_window.h` and `.cpp`

4.2. Implement DocumentWindow class:

```cpp
// document_window.cpp

DocumentWindow::DocumentWindow()
    : id_(generate_id())
    , display_name_("Untitled")
    , modified_(false)
    , window_manager_(blueprint_)
{
    window_manager_.root();  // Create root window
}

DocumentWindow::DocumentWindow(const std::string& filepath)
    : DocumentWindow()
{
    load(filepath);
}

std::string DocumentWindow::get_title_with_modified() const {
    std::string title = display_name_;
    if (modified_) {
        title += "*";
    }
    return title;
}

void DocumentWindow::render() {
    std::string window_id = get_window_id();

    // Dock to center area
    ImGui::SetNextWindowDockID(
        ImGui::GetID("CenterDock"),
        ImGuiCond_FirstUseEver
    );

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_MenuBar |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;

    if (ImGui::Begin(window_id.c_str(), &open_, flags)) {
        // Track active document
        if (ImGui::IsWindowHovered()) {
            WindowSystem::instance().set_active_document(this);
        }

        // Menu bar
        if (ImGui::BeginMenuBar()) {
            render_menu_bar();
            ImGui::EndMenuBar();
        }

        // Canvas
        render_canvas();
    }
    ImGui::End();
}

void DocumentWindow::render_menu_bar() {
    // File menu
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New", "Ctrl+N")) {
            WindowSystem::instance().create_document();
        }
        if (ImGui::MenuItem("Open...", "Ctrl+O")) {
            // TODO: Open dialog
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save", "Ctrl+S", false, modified_)) {
            if (filepath_.empty()) {
                // TODO: Save As dialog
            } else {
                save(filepath_);
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Close", false,
            WindowSystem::instance().document_count() > 1)) {
            WindowSystem::instance().close_document(*this);
        }
        ImGui::EndMenu();
    }

    // Edit menu
    if (ImGui::BeginMenu("Edit")) {
        bool has_sel = !input().selected_nodes().empty();
        if (ImGui::MenuItem("Delete", "Del", false, has_sel)) {
            InputResult r = input().on_key(Key::Delete);
            // TODO: Apply input result
        }
        ImGui::EndMenu();
    }

    // View menu
    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Zoom In", "Ctrl++")) {
            scene().viewport().zoom *= 1.1f;
            scene().viewport().clamp_zoom();
        }
        if (ImGui::MenuItem("Zoom Out", "Ctrl+-")) {
            scene().viewport().zoom /= 1.1f;
            scene().viewport().clamp_zoom();
        }
        if (ImGui::MenuItem("Reset Zoom", "Ctrl+0")) {
            scene().viewport().zoom = 1.0f;
        }
        ImGui::EndMenu();
    }
}

void DocumentWindow::render_canvas() {
    // ... (COPY existing canvas rendering logic from an24_editor.cpp)
    // Use scene(), input(), wire_manager() accessors
    // Use blueprint_, simulation_ members
}
```

4.3. Implement simulation control methods (copy from EditorApp):
- `start_simulation()`
- `stop_simulation()`
- `update_simulation()`
- `rebuild_simulation()`

4.4. Implement node content sync methods:
- `update_node_content_from_simulation()`
- `reset_node_content()`

4.5. Implement input handling methods:
- `trigger_switch()`
- `hold_button_press()`
- `hold_button_release()`
- `add_component()`
- `add_blueprint()`
- `open_sub_window()`

4.6. Implement file I/O:
- `save()`
- `load()`

**Test**: Create document, add components, save/load

---

### Phase 5: WindowSystem (Main Manager)
**Priority**: CRITICAL
**Estimated effort**: 4-6 hours

**🎯 TDD Approach**: Write failing tests for document lifecycle (create, open, close, switch) FIRST.

**Tasks**:

5.1. Create `src/editor/visual/windows/window_system.h` and `.cpp`

5.2. Implement WindowSystem class:

```cpp
// window_system.cpp

WindowSystem::WindowSystem()
    : inspector_("inspector")
    , properties_()
{
    // Load type registry at startup
    type_registry_ = an24::load_type_registry();

    // Create initial untitled document
    create_document();
}

WindowSystem& WindowSystem::instance() {
    static WindowSystem instance;
    return instance;
}

DocumentWindow& WindowSystem::create_document() {
    auto doc = std::make_unique<DocumentWindow>();
    DocumentWindow* doc_ptr = doc.get();

    doc->id_ = generate_document_id();
    doc->type_registry_ = &type_registry_;

    documents_.push_back(std::move(doc));
    set_active_document(doc_ptr);

    return *doc_ptr;
}

DocumentWindow* WindowSystem::open_document(const std::string& path) {
    // Check if already open
    if (DocumentWindow* existing = find_document_by_path(path)) {
        set_active_document(existing);
        return existing;
    }

    // Create new document and load
    auto doc = std::make_unique<DocumentWindow>(path);
    if (!doc->is_open()) {
        // Load failed
        return nullptr;
    }

    DocumentWindow* doc_ptr = doc.get();
    doc->id_ = generate_document_id();
    doc->type_registry_ = &type_registry_;

    documents_.push_back(std::move(doc));
    set_active_document(doc_ptr);

    return doc_ptr;
}

bool WindowSystem::close_document(DocumentWindow& doc) {
    if (doc.is_modified()) {
        if (!prompt_save(doc)) {
            return false;  // User cancelled
        }
    }

    // Remove from documents vector
    documents_.erase(
        std::remove_if(documents_.begin(), documents_.end(),
                      [&doc](const auto& ptr) { return ptr.get() == &doc; }),
        documents_.end()
    );

    // Update active document
    if (active_document_ == &doc) {
        active_document_ = documents_.empty() ? nullptr : documents_.front().get();
    }

    // Update inspector if it was showing this document
    if (inspector_.current_document_ == &doc) {
        inspector_.set_document(active_document_);
    }

    return true;
}

void WindowSystem::set_active_document(DocumentWindow* doc) {
    if (active_document_ != doc) {
        active_document_ = doc;

        // Update inspector to show new document
        inspector_.set_document(doc);
    }
}

DocumentWindow* WindowSystem::find_document_by_id(const std::string& id) {
    for (auto& doc : documents_) {
        if (doc->get_id() == id) {
            return doc.get();
        }
    }
    return nullptr;
}

DocumentWindow* WindowSystem::find_document_by_path(const std::string& path) {
    for (auto& doc : documents_) {
        if (doc->get_filepath() == path) {
            return doc.get();
        }
    }
    return nullptr;
}

std::string WindowSystem::generate_document_id() {
    return "doc-" + std::to_string(next_document_id_++);
}

void WindowSystem::render() {
    // Remove closed documents first
    remove_closed_documents();

    // Render inspector (docked left)
    inspector_.render();

    // Render properties (modal, only when open)
    if (properties_.is_open()) {
        properties_.render();
    }

    // Render all document windows
    for (auto& doc : documents_) {
        if (doc->is_open()) {
            doc->render();
        }
    }

    // Render sub-windows for each document
    for (auto& doc : documents_) {
        render_sub_windows(*doc);
    }

    // Render context menus
    render_context_menus();
}

void WindowSystem::remove_closed_documents() {
    documents_.erase(
        std::remove_if(documents_.begin(), documents_.end(),
                      [](const auto& doc) { return !doc->is_open(); }),
        documents_.end()
    );

    if (active_document_ && !active_document_->is_open()) {
        active_document_ = documents_.empty() ? nullptr : documents_.front().get();
        inspector_.set_document(active_document_);
    }
}

void WindowSystem::render_sub_windows(DocumentWindow& doc) {
    WindowManager& wm = doc.get_window_manager();
    wm.removeClosedWindows();

    for (auto& win_ptr : wm.windows()) {
        BlueprintWindow& win = *win_ptr;

        if (win.group_id.empty()) continue;  // Skip root
        if (!win.open) continue;

        // Render sub-window (floating, not docked)
        ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);

        std::string title = win.title + "###" + doc.get_id() + ":" + win.group_id;

        if (ImGui::Begin(title.c_str(), &win.open,
                        ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_NoScrollWithMouse)) {
            // Render canvas for sub-window
            // TODO: Extract canvas rendering to reusable function
            render_canvas_for_window(doc, win);
        }

        ImGui::End();
    }
}
```

5.3. Implement `render_context_menus()` in window_system.cpp

**Test**: Create multiple documents, switch between them

---

### Phase 6: Main Loop Rewrite
**Priority**: CRITICAL
**Estimated effort**: 3-4 hours

**Tasks**:

6.1. Rewrite `examples/an24_editor.cpp` to use WindowSystem

```cpp
// an24_editor.cpp - new main loop

int main(int argc, char** argv) {
    // ... SDL/ImGui setup (KEEP as-is) ...

    // OLD: EditorApp app;
    // NEW: WindowSystem is singleton
    WindowSystem& ws = WindowSystem::instance();

    // Load default file if provided
    if (argc > 1) {
        ws.open_document(argv[1]);
    }

    // Setup ImGui docking layout
    setup_docking_layout();

    bool show_inspector = true;

    while (running) {
        // ... event handling (KEEP as-is) ...
        // ImGui::NewFrame() (KEEP as-is)

        // ================================================================
        // GLOBAL DOCKSPACE
        // ================================================================
        render_main_dockspace();

        // ================================================================
        // RENDER ALL WINDOWS (delegated to WindowSystem)
        // ================================================================
        ws.render();

        // ================================================================
        // UPDATE SIMULATION
        // ================================================================
        if (DocumentWindow* doc = ws.active_document()) {
            doc->update_simulation(io.DeltaTime);
            doc->update_node_content_from_simulation(ws.type_registry());
        }

        // ================================================================
        // KEYBOARD SHORTCUTS (for active document)
        // ================================================================
        handle_shortcuts(ws);

        // ================================================================
        // RENDER
        // ================================================================
        ImGui::Render();
        // ... GL rendering (KEEP as-is) ...
    }

    // ... cleanup (KEEP as-is) ...
}

void render_main_dockspace() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    ImGui::Begin("MainDockSpace", nullptr, flags);
    ImGui::PopStyleVar(3);

    static bool layout_setup = false;
    if (!layout_setup) {
        layout_setup = true;

        ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);

        ImGuiID left, center, right;
        ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.20f,
                                   &left, &center);
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.25f,
                                   &right, &center);

        ImGui::DockBuilderDockWindow("Inspector", left);
        ImGui::DockBuilderDockWindow("Properties", right);
        // Center is for document tabs (auto-docked)

        ImGui::DockBuilderFinish(dockspace_id);
    }

    ImGui::End();
}

void handle_shortcuts(WindowSystem& ws) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard) return;

    DocumentWindow* doc = ws.active_document();
    if (!doc) return;

    // Simulation toggle (Space)
    if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
        if (doc->is_simulation_running()) {
            doc->stop_simulation();
        } else {
            doc->start_simulation();
        }
    }

    // New document (Ctrl+N)
    if ((io.KeyCtrl || io.KeySuper) && ImGui::IsKeyPressed(ImGuiKey_N)) {
        ws.create_document();
    }

    // Open (Ctrl+O)
    if ((io.KeyCtrl || io.KeySuper) && ImGui::IsKeyPressed(ImGuiKey_O)) {
        // TODO: Open dialog
    }

    // Save (Ctrl+S)
    if ((io.KeyCtrl || io.KeySuper) && ImGui::IsKeyPressed(ImGuiKey_S)) {
        if (doc->is_modified()) {
            if (doc->get_filepath().empty()) {
                // TODO: Save As dialog
            } else {
                doc->save(doc->get_filepath());
            }
        }
    }

    // Delete
    if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        InputResult r = doc->input().on_key(Key::Delete);
        // TODO: Apply result
    }

    // Zoom
    if ((io.KeyCtrl || io.KeySuper) && ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)) {
        doc->scene().viewport().zoom *= 1.1f;
        doc->scene().viewport().clamp_zoom();
    }
    // ... etc
}
```

**Test**: Verify all shortcuts work for active document

---

### Phase 7: CMake Integration
**Priority**: CRITICAL
**Estimated effort**: 1 hour

**Tasks**:

7.1. Update `src/editor/CMakeLists.txt`

```cmake
# Window system (OOP architecture)
set(EDITOR_WINDOW_SOURCES
    visual/windows/window.cpp
    visual/windows/panel_window.cpp
    visual/windows/document_window.cpp
    visual/windows/inspector_window.cpp
    visual/windows/properties_window.cpp
    visual/windows/window_system.cpp
)

set(EDITOR_WINDOW_HEADERS
    visual/windows/iwindow.h
    visual/windows/window.h
    visual/windows/panel_window.h
    visual/windows/document_window.h
    visual/windows/inspector_window.h
    visual/windows/properties_window.h
    visual/windows/window_system.h
)

# Add to target
target_sources(an24_editor PRIVATE ${EDITOR_WINDOW_SOURCES} ${EDITOR_WINDOW_HEADERS})
```

7.2. Build and verify no compilation errors

---

### Phase 8: Testing & Bug Fixes
**Priority**: HIGH
**Estimated effort**: 4-6 hours

**Tasks**:

8.1. Manual testing checklist (same as before)

8.2. Unit tests for window classes:

```cpp
// tests/document_window_tests.cpp

TEST(DocumentWindow, CreateUntitled) {
    DocumentWindow doc;
    EXPECT_EQ(doc.get_display_name(), "Untitled");
    EXPECT_FALSE(doc.is_modified());
    EXPECT_TRUE(doc.get_filepath().empty());
}

TEST(DocumentWindow, ModifiedIndicator) {
    DocumentWindow doc;
    EXPECT_FALSE(doc.get_title().find('*') != std::string::npos);

    doc.mark_modified();
    EXPECT_TRUE(doc.get_title().find('*') != std::string::npos);
}

TEST(DocumentWindow, SaveLoad) {
    DocumentWindow doc;
    doc.add_component("Battery", Pt(0, 0));
    doc.save("/tmp/test.json");

    DocumentWindow doc2;
    doc2.load("/tmp/test.json");
    EXPECT_EQ(doc2.get_blueprint().nodes.size(), 1);
}
```

---

## 📊 Effort Estimation (Updated for OOP)

| Phase | Tasks | Effort | Priority |
|-------|-------|--------|----------|
| 1. Window Infrastructure | IWindow, Window, PanelWindow | 2-3h | CRITICAL |
| 2. InspectorWindow | First panel implementation | 2-3h | CRITICAL |
| 3. PropertiesWindow | Move to PanelWindow base | 1-2h | CRITICAL |
| 4. DocumentWindow | Main document class | 6-8h | CRITICAL |
| 5. WindowSystem | Manager implementation | 4-6h | CRITICAL |
| 6. Main Loop | Rewrite for WindowSystem | 3-4h | CRITICAL |
| 7. CMake | Build integration | 1h | CRITICAL |
| 8. Testing | Manual + unit tests | 4-6h | HIGH |
| **TOTAL** | | **23-33 hours** | |

> **⏱️ Note**: Time estimates assume TDD approach (writing tests first). If skipping tests, implementation will be faster but will require debugging time later. We recommend **NOT skipping tests** - they serve as executable specification and prevent regressions.

---

## 🚨 Key Design Decisions (OOP Architecture)

### 1. Window Hierarchy
```
IWindow (interface)
├── Window (base implementation)
    ├── PanelWindow (global panels)
    │   ├── InspectorWindow
    │   └── PropertiesWindow
    └── DocumentWindow (MDI documents)
```

### 2. Separation of Concerns
- **IWindow**: Minimal interface for all windows
- **Window**: Common functionality (ID, open/close, docking)
- **PanelWindow**: Base for global panels (auto-dock to sides)
- **DocumentWindow**: Full-featured document with menu bar + canvas

### 3. Single Responsibility
- **DocumentWindow**: Manages ONE document (blueprint + simulation)
- **WindowSystem**: Manages ALL windows and documents
- **InspectorWindow**: Renders inspector for active document
- **PropertiesWindow**: Edits node parameters (modal)

### 4. Polymorphism
- All windows can be rendered via `IWindow::render()`
- PanelWindow auto-handles docking and menu bars
- DocumentWindow has its own menu bar (per-window)

---

## 📝 Implementation Notes for Developer

### Do NOT Change These Files
```
src/editor/visual/           (Rendering logic - no changes needed)
src/editor/input/            (Input handling - no changes needed)
src/editor/router/           (Wire routing - no changes needed)
src/editor/data/             (Data structures - no changes needed, but see below)
src/jit_solver/              (Simulation - no changes needed)
src/json_parser/             (JSON parsing - no changes needed)
```

### Focus Changes On
```
examples/an24_editor.cpp     ← 80% of changes here (main loop rewrite)
src/editor/visual/windows/   ← 20% of changes here (new OOP classes)
```

### ⚠️ CRITICAL: Future-Proof JSON Serialization

**See the detailed "FUTURE ARCHITECTURE NOTE" section above for context.**

When implementing `Document::save()` and `Document::load()`:

```cpp
// ✅ RECOMMENDED - Use abstraction layer
bool Document::save(const std::string& path) {
    // Delegate to persistence layer (future-proof for blueprint refs)
    std::ofstream out(path);
    nlohmann::json j = blueprint_.serialize();  // Keep in Blueprint class
    out << j.dump(2);
    // ...
}

bool Document::load(const std::string& path) {
    // Delegate to persistence layer
    std::ifstream in(path);
    nlohmann::json j;
    in >> j;
    blueprint_ = Blueprint::deserialize(j);  // Keep in Blueprint class
    // ...
}
```

**AVOID** directly accessing `blueprint_.nodes` in Document serialization - this will break when we add blueprint references.

### Key Principles
1. **TDD: Failing Tests First** 🎯 - Write unit tests BEFORE implementation (RED → GREEN → REFACTOR)
2. **Minimal refactoring**: Copy working code from EditorApp to DocumentWindow, don't rewrite
3. **Per-document isolation**: Each DocumentWindow owns its Blueprint + Simulator
4. **Active document tracking**: Use ImGui hover state to track which doc is active
5. **Global UI**: Inspector and Properties are global, but operate on active doc
6. **Sub-windows stay floating**: No docking for collapsed group windows
7. **Persistence abstraction**: Keep save/load separate from Document logic

### Code Organization Tips

**DO**:
- Put rendering code in `DocumentWindow::render_canvas()` (private method)
- Put menu rendering in `DocumentWindow::render_menu_bar()` (private method)
- Delegate to `WindowSystem` for cross-document operations
- Use accessors: `scene()`, `input()`, `wire_manager()`, `blueprint()`

**DON'T**:
- Don't mix rendering logic with business logic
- Don't access other documents from within DocumentWindow
- Don't hardcode assumptions about JSON format
- Don't create circular dependencies between windows

### Testing Strategy

#### 🎯 Development Philosophy: Failing Tests First (TDD)

**We prefer Test-Driven Development (TDD)**: Write failing tests first, then implement the feature to make tests pass.

**Benefits**:
- Tests serve as executable specification
- Forces thinking about API design before implementation
- Immediate feedback when code breaks
- Safer refactoring (run tests after changes)

**Workflow for each phase**:
1. Write unit test for the new class/method
2. Run tests → **expect RED (failing)**
3. Implement minimal code to make tests pass
4. Run tests → **expect GREEN (passing)**
5. Refactor if needed
6. Commit

**Example**:
```cpp
// STEP 1: Write failing test first
TEST(DocumentWindow, CreateUntitled) {
    DocumentWindow doc;
    EXPECT_EQ(doc.get_display_name(), "Untitled");  // ← Will fail, not implemented yet
}

// STEP 2: Implement minimal code
DocumentWindow::DocumentWindow()
    : display_name_("Untitled")  // ← Just enough to pass test
{}

// STEP 3: Test passes, move to next feature
```

#### Test Organization

1. **Unit tests**: Test each class in isolation (write these FIRST!)
2. **Integration tests**: Test WindowSystem with multiple documents
3. **Manual tests**: Open/edit/save/close multiple documents
4. **Memory tests**: Run with valgrind/ASan to check for leaks

### Debugging Tips

- Use `get_type_name()` to identify window types in logs
- Check `active_document()` when context menu doesn't work
- Verify `is_modified()` flag when testing save/load
- Use ImGui metrics (Ctrl+Shift+M) to inspect docking layout

---

## ✅ Acceptance Criteria (Updated)

Implementation is COMPLETE when:

1. ✅ OOP window hierarchy with IWindow interface
2. ✅ Multiple documents can be opened (DocumentWindow instances)
3. ✅ Documents appear as dockable tabs (ImGui docking)
4. ✅ Each document has its own menu bar (DocumentWindow::render_menu_bar)
5. ✅ Inspector/Properties inherit from PanelWindow
6. ✅ Inspector auto-switches to active document
7. ✅ Only active document's simulation runs
8. ✅ Modified indicator (*) works correctly
9. ✅ All existing tests pass
10. ✅ No memory leaks

---

**Document Version**: 2.0 (OOP Architecture)
**Last Updated**: 2026-03-11
**Ready for Implementation**: ✅ YES
**Architecture Style**: OOP with inheritance (matches src/editor/visual/)
