# UI/UX Refactoring Plan

## Goal

Refactor `an24_editor.cpp` (870 lines) into clean, modular, SOLID-compliant architecture.

## Current State

```
an24_editor.cpp (870 lines)
├── SDL/OpenGL init
├── Platform config path
├── Main loop
├── ImGui frame
├── Main menu (100 lines)
├── Keyboard shortcuts
├── Inspector panel
├── Document tabs (50 lines)
├── Canvas rendering (200 lines lambda)
├── Sub-window rendering (100 lines)
├── Context menus (50 lines)
├── Color picker
├── Bake-in dialog
└── Properties window
```

## Target Architecture

```
src/editor/
├── platform/
│   └── config_paths.h/cpp        # Platform-specific paths
├── window_system.h/cpp           # (exists) Document management
├── document.h/cpp                # (exists) Single document
├── recent_files.h/cpp            # (exists) Recent files list
└── visual/
    ├── dialogs/
    │   └── file_dialogs.h/cpp    # (exists) NFD wrappers
    ├── menu/
    │   └── main_menu.h/cpp       # (exists) Main menu bar
    ├── tabs/
    │   └── document_tabs.h/cpp   # (exists) Document tabs
    ├── canvas/
    │   ├── canvas_renderer.h/cpp # Canvas rendering
    │   └── canvas_input.h/cpp    # Input handling (extract from canvas_input.cpp)
    ├── panels/
    │   ├── inspector_panel.h/cpp # Inspector panel
    │   └── properties_panel.h/cpp# Properties window
    ├── popups/
    │   ├── context_menu.h/cpp    # Context menu (add component/blueprint)
    │   ├── color_picker.h/cpp    # Color picker popup
    │   └── bake_in_dialog.h/cpp  # Bake-in confirmation
    ├── windows/
    │   └── sub_window.h/cpp      # Sub-blueprint windows
    └── editor_ui.h/cpp           # Main UI coordinator

examples/an24_editor.cpp (~100 lines)
├── SDL/OpenGL init
├── Main loop (calls editor_ui.render())
└── Cleanup
```

## Phases

### Phase 1: Foundation (DONE ✅)
- [x] RecentFiles with platform-aware paths
- [x] Document::isPristine()
- [x] dialogs::openBlueprint(), dialogs::saveBlueprint()
- [x] MainMenu class
- [x] DocumentTabs class
- [x] Tests (1404 passing)

### Phase 2: Canvas Refactoring
- [x] CanvasRenderer::render() - grid, blueprint, tooltips ✅
- [x] CanvasInput::handleInput() - mouse/keyboard (split from CanvasInput class)
- [x] NodeContentRenderer - gauges, switches, sliders
- [x] Integration with existing BlueprintRenderer

### Phase 3: Panels Extraction
- [x] InspectorPanel - left panel with selection handling
- [x] DocumentArea - tabs + canvas rendering

### Phase 3: Panels Extraction
- [ ] InspectorPanel - left panel with selection handling
- [ ] PropertiesPanel - node properties window

### Phase 4: Popups Extraction
- [ ] ContextMenu - add component/blueprint menu
- [ ] ColorPickerPopup - node color picker
- [ ] BakeInDialog - sub-blueprint bake-in confirmation

### Phase 5: Sub-Windows
- [ ] SubWindowRenderer - sub-blueprint window rendering
- [ ] SubWindowManager - coordinate with WindowManager

### Phase 6: EditorUI Coordinator
- [ ] EditorUI class - orchestrates all UI components
- [ ] Layout management - inspector, splitter, canvas
- [ ] Event routing - keyboard shortcuts

### Phase 7: Main Loop Simplification
- [ ] Refactor an24_editor.cpp to use EditorUI
- [ ] Remove process_window lambda
- [ ] ~100 lines main file

### Phase 8: Testing & Polish
- [ ] Integration tests for UI components
- [ ] Edge case tests
- [ ] Memory leak checks
- [ ] Performance validation

## File Size Targets

| File | Current | Target |
|------|---------|--------|
| an24_editor.cpp | 870 | ~100 |
| canvas_renderer.cpp | - | ~150 |
| canvas_input.cpp | 608 | ~300 (split) |
| editor_ui.cpp | - | ~200 |
| main_menu.cpp | 142 | 142 |
| document_tabs.cpp | 36 | 36 |

## SOLID Compliance

- **S**ingle Responsibility: Each class does one thing
- **O**pen/Closed: Add new panels/popups without changing EditorUI
- **L**iskov Substitution: All panels implement IPanel interface
- **I**nterface Segregation: Small, focused interfaces
- **D**ependency Inversion: EditorUI depends on abstractions

## DRY Compliance

- No duplicate dialog code
- No duplicate menu building
- Shared canvas rendering logic
- Unified input handling

## Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| Breaking existing functionality | Keep tests passing at each phase |
| LSP/compile errors | Incremental changes, build frequently |
| Circular dependencies | Forward declarations, careful includes |
| Performance regression | Profile before/after |
