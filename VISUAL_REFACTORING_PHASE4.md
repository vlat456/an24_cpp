# Visual Layer Refactoring - Phase 4

## Goal

Restructure `src/editor/visual/` for better modularity and SOLID compliance.
Target: ~100-150 lines per file, max 200 for `.cpp`.

## Directory Structure

```
src/editor/visual/
├── node/
│   ├── node.h/.cpp                  # VisualNode base class (97/392 lines)
│   ├── bounds.h                     # existing
│   ├── edges.h                      # existing
│   ├── widget/
│   │   ├── widget_base.h/.cpp       # Base Widget class (44/10 lines)
│   │   ├── primitives/
│   │   │   ├── label.h/.cpp         # (24/28 lines)
│   │   │   ├── circle.h/.cpp        # (16/16 lines)
│   │   │   └── spacer.h/.cpp        # (11/11 lines)
│   │   ├── containers/
│   │   │   ├── column.h/.cpp        # (20/56 lines)
│   │   │   ├── row.h/.cpp           # (20/56 lines)
│   │   │   └── container.h/.cpp     # (20/31 lines)
│   │   └── content/
│   │       ├── header_widget.h/.cpp # (38/40 lines)
│   │       ├── type_name_widget.h/.cpp # (18/32 lines)
│   │       ├── switch_widget.h/.cpp # (25/43 lines)
│   │       ├── vertical_toggle.h/.cpp # (26/61 lines)
│   │       └── voltmeter_widget.h/.cpp # (36/78 lines)
│   └── types/
│       ├── bus_node.h/.cpp          # existing
│       ├── group_node.h/.cpp        # existing
│       ├── text_node.h/.cpp         # existing
│       └── ref_node.h/.cpp          # existing
├── renderer/
│   ├── wire/
│   │   ├── wire_renderer.h/.cpp     # main orchestration
│   │   ├── crossing.h/.cpp          # crossing detection + arcs
│   │   └── polyline.h/.cpp          # polyline building + gaps
│   └── node/
│       └── node_renderer.h/.cpp     # existing
├── hittest/
│   ├── hittest.h                    # HitResult struct
│   ├── nodes.cpp                    # hit_test()
│   └── ports.cpp                    # hit_test_ports()
└── spatial/
    ├── grid.h                       # declarations
    └── grid.cpp                     # implementation
```

## Tasks

### Phase 4.1: node/widget/ (Priority: HIGH) ✅ COMPLETE

Split `widget.h/.cpp` and `layout.h/.cpp` into individual widget files.

- [x] Create `node/widget/` directory
- [x] Move `widget_base.h` to `node/widget/`
- [x] Extract `label.h/.cpp` from layout.cpp
- [x] Extract `circle.h/.cpp` from layout.cpp
- [x] Extract `spacer.h/.cpp` from layout.cpp
- [x] Extract `column.h/.cpp` from layout.cpp
- [x] Extract `row.h/.cpp` from layout.cpp
- [x] Extract `container.h/.cpp` from layout.cpp
- [x] Extract `header_widget.h/.cpp` from widget.cpp
- [x] Extract `type_name_widget.h/.cpp` from widget.cpp
- [x] Extract `switch_widget.h/.cpp` from widget.cpp
- [x] Extract `vertical_toggle.h/.cpp` from widget.cpp
- [x] Extract `voltmeter_widget.h/.cpp` from widget.cpp
- [x] Update all #include paths
- [x] Update CMakeLists.txt
- [x] Delete old `widget.h/.cpp` and `layout.h/.cpp`
- [x] Build and test

**Results:**
- 12 new widget files created in 3 subdirs (primitives/, containers/, content/)
- All files under 80 lines (target was 100-150)
- All 1430 tests pass
- Total: ~760 lines across 25 files (avg 30 lines per file)

### Phase 4.2: node/layout_builder (Priority: HIGH) - DEFERRED

Extract layout building logic from `node.cpp`.

- [ ] ~~Create `layout_builder.h` with `PortSlot` struct~~
- [ ] ~~Create `layout_builder.cpp` with `buildLayout()` function~~
- [ ] ~~Refactor `node.cpp` to use layout_builder~~
- [ ] Build and test

**Status:** Deferred. `buildLayout()` is tightly coupled to `VisualNode` state
(`layout_`, `port_slots_`, `content_widget_`, `node_content_`). Extraction
would require either:
1. Changing `layout_` from `Column` to `std::unique_ptr<Column>`
2. Creating a builder that populates via reference
3. Keeping as-is and documenting as technical debt

Current `node.cpp` is 392 lines - acceptable for now.

### Phase 4.3: renderer/wire/ (Priority: MEDIUM) - PENDING

Split `wire_renderer.cpp` into focused modules.

- [ ] Create `renderer/wire/` directory
- [ ] Extract `crossing.h/.cpp` (crossing detection + arc drawing)
- [ ] Extract `polyline.h/.cpp` (polyline building + gap insertion)
- [ ] Refactor `wire_renderer.cpp` to use new modules
- [ ] Update CMakeLists.txt
- [ ] Build and test

### Phase 4.4: hittest/ (Priority: MEDIUM) - PENDING

Move and split hittest functionality.

- [ ] Create `hittest/` directory
- [ ] Move `hittest.h` → `hittest/hittest.h`
- [ ] Extract `nodes.cpp` from hittest.cpp
- [ ] Extract `ports.cpp` from hittest.cpp
- [ ] Update CMakeLists.txt
- [ ] Build and test

### Phase 4.5: spatial/ (Priority: LOW) - PENDING

Extract implementation from header-only `spatial_grid.h`.

- [ ] Create `spatial/` directory
- [ ] Split `spatial_grid.h` → `grid.h` + `grid.cpp`
- [ ] Update CMakeLists.txt
- [ ] Build and test

## Verification

After each phase:
1. `cmake --build build -j$(nproc)` — must compile
2. `cd build && ctest` — all tests pass
3. Line count check: `wc -l` on new files

## Current Status

**Phase 4.1:** ✅ COMPLETE
**Phase 4.2:** ⏸️ DEFERRED (tight coupling)
**Phase 4.3:** PENDING
**Phase 4.4:** PENDING
**Phase 4.5:** PENDING

## File Line Counts (Phase 4.1)

| File | Lines | Status |
|------|-------|--------|
| node.h | 97 | ✅ |
| node.cpp | 392 | ⚠️ Above target |
| widget/widget_base.h | 44 | ✅ |
| widget/widget_base.cpp | 10 | ✅ |
| widget/primitives/label.h | 24 | ✅ |
| widget/primitives/label.cpp | 28 | ✅ |
| widget/primitives/circle.h | 16 | ✅ |
| widget/primitives/circle.cpp | 16 | ✅ |
| widget/primitives/spacer.h | 11 | ✅ |
| widget/primitives/spacer.cpp | 11 | ✅ |
| widget/containers/column.h | 20 | ✅ |
| widget/containers/column.cpp | 56 | ✅ |
| widget/containers/row.h | 20 | ✅ |
| widget/containers/row.cpp | 56 | ✅ |
| widget/containers/container.h | 20 | ✅ |
| widget/containers/container.cpp | 31 | ✅ |
| widget/content/header_widget.h | 38 | ✅ |
| widget/content/header_widget.cpp | 40 | ✅ |
| widget/content/type_name_widget.h | 18 | ✅ |
| widget/content/type_name_widget.cpp | 32 | ✅ |
| widget/content/switch_widget.h | 25 | ✅ |
| widget/content/switch_widget.cpp | 43 | ✅ |
| widget/content/vertical_toggle.h | 26 | ✅ |
| widget/content/vertical_toggle.cpp | 61 | ✅ |
| widget/content/voltmeter_widget.h | 36 | ✅ |
| widget/content/voltmeter_widget.cpp | 78 | ✅ |
