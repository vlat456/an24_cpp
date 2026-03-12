# Visual Layer Refactoring - Phase 4

## Goal

Restructure `src/editor/visual/` for better modularity and SOLID compliance.
Target: ~100-150 lines per file, max 200 for `.cpp`.

## Directory Structure

```
src/editor/visual/
├── node/
│   ├── node.h/.cpp                  # VisualNode base class
│   ├── layout_builder.h/.cpp        # buildLayout() + PortSlot
│   ├── bounds.h                     # existing
│   ├── edges.h                      # existing
│   ├── widget/
│   │   ├── widget_base.h/.cpp       # Base Widget class
│   │   ├── primitives/
│   │   │   ├── label.h/.cpp
│   │   │   ├── circle.h/.cpp
│   │   │   └── spacer.h/.cpp
│   │   ├── containers/
│   │   │   ├── column.h/.cpp
│   │   │   ├── row.h/.cpp
│   │   │   └── container.h/.cpp
│   │   └── content/
│   │       ├── header_widget.h/.cpp
│   │       ├── type_name_widget.h/.cpp
│   │       ├── switch_widget.h/.cpp
│   │       ├── vertical_toggle.h/.cpp
│   │       └── voltmeter_widget.h/.cpp
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
- 12 new widget files created
- All files under 80 lines (target was 100-150)
- 1430 tests pass
- Total: 760 lines across 25 files (avg 30 lines per file)

### Phase 4.2: node/layout_builder (Priority: HIGH)

Extract layout building logic from `node.cpp`.

- [ ] Create `layout_builder.h` with `PortSlot` struct
- [ ] Create `layout_builder.cpp` with `buildLayout()` function
- [ ] Refactor `node.cpp` to use layout_builder
- [ ] Build and test

### Phase 4.3: renderer/wire/ (Priority: MEDIUM)

Split `wire_renderer.cpp` into focused modules.

- [ ] Create `renderer/wire/` directory
- [ ] Extract `crossing.h/.cpp` (crossing detection + arc drawing)
- [ ] Extract `polyline.h/.cpp` (polyline building + gap insertion)
- [ ] Refactor `wire_renderer.cpp` to use new modules
- [ ] Update CMakeLists.txt
- [ ] Build and test

### Phase 4.4: hittest/ (Priority: MEDIUM)

Move and split hittest functionality.

- [ ] Create `hittest/` directory
- [ ] Move `hittest.h` → `hittest/hittest.h`
- [ ] Extract `nodes.cpp` from hittest.cpp
- [ ] Extract `ports.cpp` from hittest.cpp
- [ ] Update CMakeLists.txt
- [ ] Build and test

### Phase 4.5: spatial/ (Priority: LOW)

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
**Phase 4.2:** PENDING
**Phase 4.3:** PENDING
**Phase 4.4:** PENDING
**Phase 4.5:** PENDING
