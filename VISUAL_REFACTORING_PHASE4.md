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
│   │   ├── polyline_builder.h/.cpp  # (17/46 lines)
│   │   ├── polyline_draw.h/.cpp     # (27/103 lines)
│   │   └── arc_draw.h/.cpp          # (22/34 lines)
│   ├── wire_renderer.h/.cpp         # main orchestration (24/77 lines)
│   └── node_renderer.h/.cpp         # existing
├── hittest/
│   ├── hittest.h                    # HitResult struct (40 lines)
│   ├── nodes.cpp                    # hit_test() (128 lines)
│   └── ports.cpp                    # hit_test_ports() (48 lines)
└── spatial/
    └── grid.h                       # SpatialGrid class (151 lines)
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

### Phase 4.3: renderer/wire/ (Priority: MEDIUM) ✅ COMPLETE

Split `wire_renderer.cpp` into focused modules.

- [x] Create `renderer/wire/` directory
- [x] Extract `polyline_builder.h/.cpp` (polyline building from wires)
- [x] Extract `polyline_draw.h/.cpp` (polyline drawing with gap insertion)
- [x] Extract `arc_draw.h/.cpp` (jump arc drawing at crossings)
- [x] Refactor `wire_renderer.cpp` to use new modules
- [x] Update CMakeLists.txt (examples/ and tests/)
- [x] Build and test

**Results:**
- `wire_renderer.cpp`: 230 → 77 lines
- `polyline_builder.h/.cpp`: 17/46 lines
- `polyline_draw.h/.cpp`: 27/103 lines  
- `arc_draw.h/.cpp`: 22/34 lines
- All 1430 tests pass

### Phase 4.4: hittest/ (Priority: MEDIUM) ✅ COMPLETE

Move and split hittest functionality.

- [x] Create `hittest/` directory
- [x] Move `hittest.h` → `hittest/hittest.h`
- [x] Extract `nodes.cpp` from hittest.cpp (hit_test function)
- [x] Extract `ports.cpp` from hittest.cpp (hit_test_ports function)
- [x] Update all #include paths
- [x] Update CMakeLists.txt
- [x] Delete old files
- [x] Build and test

**Results:**
- `hittest.h`: 47 → 40 lines
- `nodes.cpp`: 128 lines
- `ports.cpp`: 48 lines
- All 1430 tests pass

### Phase 4.5: spatial/ (Priority: LOW) ✅ COMPLETE

Move spatial_grid.h to spatial subdirectory.

- [x] Create `spatial/` directory
- [x] Move `spatial_grid.h` → `spatial/grid.h`
- [x] Update all #include paths
- [x] Build and test

**Results:**
- `spatial/grid.h`: 151 lines (header-only, no split needed)
- All 1430 tests pass

### Phase 4.6: Wire Module Unit Tests (Priority: MEDIUM) ✅ COMPLETE

Added `test_wire_modules.cpp` with 23 unit tests covering the extracted wire modules.

- [x] Write unit tests for `polyline_builder` (6 tests)
- [x] Write unit tests for `polyline_draw::classify_crossings_by_segment` (6 tests)
- [x] Write unit tests for `polyline_draw::draw_polyline_with_gaps` (3 tests)
- [x] Write unit tests for `arc_draw` (6 tests)
- [x] Write integration tests combining crossings + classify + draw (2 tests)
- [x] Add `wire_module_tests` target to `tests/CMakeLists.txt`
- [x] Build and pass all 23 new tests
- [x] Full suite: 1453 tests pass (was 1430)

## Verification

After each phase:
1. `cmake --build build -j$(nproc)` — must compile ✅
2. `cd build && ctest` — all 1453 tests pass ✅
3. Line count check: `wc -l` on new files ✅

## Current Status

**Phase 4.1:** ✅ COMPLETE
**Phase 4.2:** ⏸️ DEFERRED (tight coupling)
**Phase 4.3:** ✅ COMPLETE
**Phase 4.4:** ✅ COMPLETE
**Phase 4.5:** ✅ COMPLETE
**Phase 4.6:** ✅ COMPLETE (wire module unit tests)

## File Line Counts (Final)

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
| renderer/wire_renderer.h | 24 | ✅ |
| renderer/wire_renderer.cpp | 77 | ✅ |
| renderer/wire/polyline_builder.h | 17 | ✅ |
| renderer/wire/polyline_builder.cpp | 46 | ✅ |
| renderer/wire/polyline_draw.h | 27 | ✅ |
| renderer/wire/polyline_draw.cpp | 103 | ✅ |
| renderer/wire/arc_draw.h | 22 | ✅ |
| renderer/wire/arc_draw.cpp | 34 | ✅ |
| hittest/hittest.h | 40 | ✅ |
| hittest/nodes.cpp | 128 | ✅ |
| hittest/ports.cpp | 48 | ✅ |
| spatial/grid.h | 151 | ✅ |
