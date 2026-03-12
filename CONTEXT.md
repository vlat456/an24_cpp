# An-24 Development Context

## Current Focus: UI/UX Refactoring

**Status:** Phase 1 of UI refactoring complete. Foundation laid for modular architecture.

**Commits:**
- `af9b3de` - WIP: RecentFiles, MainMenu, DocumentTabs, CanvasRenderer
- `3bed6c5` - UI refactoring plan + edge case tests

**Tests:** 1411/1411 passing (1 skipped)

---

## Recent Accomplishments

### Recent Files System (COMPLETE ✅)

**Files:**
- `src/editor/recent_files.h/cpp` - Pure business logic, platform-aware paths
- `tests/test_recent_files.cpp` - 21 tests

**Features:**
- MRU list (max 10)
- Platform-aware config paths (Windows/macOS/Linux)
- Persistence via `loadFrom()`/`saveTo()`
- Duplicates handled (moved to front)
- Non-existent files filtered on load

### Document Management (COMPLETE ✅)

**Features:**
- `Document::isPristine()` - true if empty, never saved
- `WindowSystem::openDocument()` - replaces pristine Untitled on first load
- Tab switching activates correct document
- Save always enabled (no modified tracking)

**Tests:** 67 Document/WindowSystem tests

### UI Components (WIP)

**Created:**
- `src/editor/visual/dialogs/file_dialogs.h/cpp` - NFD wrappers
- `src/editor/visual/menu/main_menu.h/cpp` - Main menu bar (142 lines)
- `src/editor/visual/tabs/document_tabs.h/cpp` - Document tabs (36 lines)
- `src/editor/visual/canvas_renderer.h/cpp` - Canvas rendering (217 lines, WIP)

---

## Next Steps (from UI_REFACTORING_PLAN.md)

### Phase 2: Canvas Refactoring
- [ ] Integrate CanvasRenderer into main loop
- [ ] Split input handling from CanvasInput
- [ ] NodeContentRenderer for gauges/switches

### Phase 3-7: Panels, Popups, EditorUI
- See `UI_REFACTORING_PLAN.md` for full plan

---

## Key Files

### Production Code
| File | Lines | Purpose |
|------|-------|---------|
| `src/editor/recent_files.h/cpp` | 78 | Recent files logic |
| `src/editor/document.h/cpp` | 400 | Single document |
| `src/editor/window_system.h/cpp` | 200 | Document management |
| `src/editor/visual/dialogs/file_dialogs.cpp` | 34 | NFD wrappers |
| `src/editor/visual/menu/main_menu.cpp` | 142 | Main menu |
| `src/editor/visual/tabs/document_tabs.cpp` | 36 | Document tabs |
| `src/editor/visual/canvas_renderer.cpp` | 217 | Canvas rendering (WIP) |

### Test Files
| File | Lines | Tests |
|------|-------|-------|
| `tests/test_recent_files.cpp` | 272 | 21 |
| `tests/test_document_window_system.cpp` | 981 | 46 |

### Design Docs
| File | Purpose |
|------|---------|
| `UI_REFACTORING_PLAN.md` | 8-phase UI refactoring |
| `JSON_FORMAT_V2_DESIGN.md` | v2 format design |
| `V2_IMPL_PLAN.md` | v2 implementation plan (mostly done) |
| `AGENTS.md` | Code style guidelines |

---

## v2 Format Migration Status

| Phase | Status |
|-------|--------|
| 1. v2 data structures | **DONE** |
| 2. Library conversion | **DONE** |
| 3. Editor persistence | **DONE** |
| 4. Direct BuildInput | Not started |
| 5. Codegen verification | Not started |
| 6. Delete v1 code | Not started |

---

## Build & Test Commands

```bash
# Build
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(sysctl -n hw.ncpu)

# Run all tests
cd build && ctest

# Run specific tests
cd build && ctest -R "RecentFiles|Document" --output-on-failure
```

---

## Architecture Principles

- **SOLID:** Single responsibility, open/closed, Liskov, interface segregation, dependency inversion
- **DRY:** No duplicate dialog/menu code
- **Small files:** Target ~150 lines per file
- **Visual hierarchy:** `src/editor/visual/` for UI components
