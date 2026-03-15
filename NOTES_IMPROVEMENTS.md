# An-24 Codebase Improvements Analysis

**Date:** 2026-03-14  
**Status:** 1236/1236 tests passing (100%)

---

## Completed Today (2026-03-14)

- [x] Full InternedId migration for all 5 visual widgets
- [x] CMake integration for `port_registry.h` regeneration
- [x] Compile-time sync guard: `static_assert(variant_size == ComponentType::_COUNT)`
- [x] **SimulationController → Simulator unification**
- [x] **Multi-domain test coverage** - 8 new tests
- [x] **Command Pattern with Undo/Redo** - FULLY IMPLEMENTED
  - `src/editor/commands/` - Atomic and compound commands
  - `src/editor/undo/undo_stack.h` - Undo/Redo stack
  - `Document` owns `UndoStack`, passed by reference to `WindowManager` → `BlueprintWindow` → `CanvasInput`
  - `CanvasInput::execute_command()`, `undo()`, `redo()`
  - Node drag, delete, grid step changes all use commands
  - Ctrl+Z/Y keyboard shortcuts work in canvas
  - Edit menu has Undo/Redo items
  - 6 new tests in `test_commands.cpp`
- [x] **Command Pattern with Automatic Inverse** - Full implementation
- [x] **UndoStack integration** - Shared across all windows per document
- [x] **Edit menu** - Undo/Redo items with keyboard shortcuts
- [x] **CanvasInput integration** - Node drag, wire, delete all use commands

---

## Command System Implementation

### Architecture

```
Document
  └─ UndoStack undo_stack_          // Single undo history per document
  └─ WindowManager window_manager_{blueprint_, undo_stack_}
       └─ BlueprintWindow
            └─ CanvasInput input    // Receives UndoStack& reference
```

### Key Files

| File | Purpose |
|------|---------|
| `src/editor/commands/commands.h` | Command types + `execute()` |
| `src/editor/commands/commands.cpp` | Command implementations |
| `src/editor/undo/undo_stack.h` | Undo/Redo stack storage |
| `src/editor/document.h` | `performUndo()` / `performRedo()` |
| `src/editor/input/canvas_input.cpp` | `execute_command()`, keyboard handlers |

### Commands Implemented

| Command | Description |
|---------|-------------|
| `CmdAddNode` | Add node to blueprint |
| `CmdRemoveNode` | Remove node + connected wires |
| `CmdMoveNode` | Move node position |
| `CmdAddWire` | Add wire connection |
| `CmdRemoveWire` | Remove wire |
| `CmdReconnectWire` | Change wire endpoint |
| `CmdSetParam` | Set node parameter |
| `CmdSetRoutingPoints` | Set wire routing points |
| `CmdSetGridStep` | Set grid step |
| `CmdSetName` | Set node display name |
| `CmdSwapBusPorts` | Swap bus port wires (self-inverse) |
| `CmdCompound` | Group multiple commands |

### Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| `Ctrl+Z` | Undo |
| `Ctrl+Shift+Z` | Redo |
| `Ctrl+Y` | Redo |

### Bug Fixed

**Infinite recursion** caused by missing `execute(Blueprint&, const AtomicCommand&)` overload. With nested `std::variant<AtomicCommand, CmdCompound>`, the visitor was calling `execute(Blueprint&, const Command&)` recursively instead of unpacking to concrete types.

---

## Remaining Improvements

### 1. Component Factory Registry ⭐ HIGH IMPACT

**Problem:** 450+ line if-else chain for 66 component types

**Complexity:** Medium

---

### 2. Fix Silent Exceptions

**Problem:** 2 instances of `catch (...) {}` in `codegen.cpp`

**Complexity:** Small

---

### 3. Resolve TODOs

| Location | Issue |
|----------|-------|
| `json_parser.cpp:398` | `|| true` hack |
| `all.h:561` | Spring damping unused |
| `canvas_input.cpp:589` | Wire auto-routing |

**Complexity:** Small-Medium

---

### 4. Undo System ⭐ HIGH IMPACT ✅ COMPLETE

**Status:** Fully implemented (2026-03-14)

**Files:**
- `src/editor/commands/commands.h` - Command definitions
- `src/editor/commands/commands.cpp` - Execute logic with inverse generation
- `src/editor/undo/undo_stack.h` - Undo/Redo stack (header-only)

**Integration:**
- `Document` owns `UndoStack`, passed by reference through `WindowManager` → `BlueprintWindow` → `CanvasInput`
- Keyboard shortcuts: `Ctrl+Z` (Undo), `Ctrl+Shift+Z`/`Ctrl+Y` (Redo)
- Edit menu: Undo/Redo items with enabled state
- Properties Window: Param changes use commands

**Tests:** 12 tests (8 basic + 4 properties window)

---

### Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| `Ctrl+Z` | Undo |
| `Ctrl+Shift+Z` | Redo |
| `Ctrl+Y` | Redo |

### Bug Fixed During Implementation

**Infinite recursion** caused by missing `execute(Blueprint&, const AtomicCommand&)` overload. With nested `std::variant<AtomicCommand, CmdCompound>`, the visitor was calling `execute(Blueprint&, const Command&)` recursively instead of unpacking to concrete types.

### Undo Integration Points

| Feature | Status |
|---------|--------|
| Node drag (mouse-up) | ✅ |
| Node delete (Del/Backspace) | ✅ |
| Grid step change ([ / ]) | ✅ |
| Wire creation | ✅ |
| Wire reconnection | ✅ |
| Bus port swap | ✅ |
| Properties window: params | ✅ |
| Properties window: name | ✅ |
| Routing point drag | ✅ |

---

## Summary

| Task | Impact | Complexity | Status |
|------|--------|------------|--------|
| Component Factory Registry | HIGH | Medium | 🔴 Pending |
| Fix Silent Exceptions | MEDIUM | Small | 🔴 Pending |
| Resolve TODOs | MEDIUM | Small | 🔴 Pending |
| **Undo System** | **HIGH** | **Medium** | ✅ **Complete** |
| Benchmark Infrastructure | LOW | Medium | 🔴 Pending |
| Enhance Error Messages | LOW | Small | 🔴 Pending |
