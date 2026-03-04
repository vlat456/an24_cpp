# A* Router Implementation Plan

## Overview
Port Rust A* wire router to C++. Triggers on 'R' key with selected wire.

## Components

### 1. Router Core (src/editor/router/)
- `grid.h` - GridPt, Dir, State types
- `algorithm.h` - A* search implementation
- `path.h` - path simplification (remove collinear points)
- `router.h` - main API: `route_around_nodes()`

### 2. Integration (src/editor/)
- `app.cpp` - handle 'R' key to reroute selected wire
- Update CMakeLists.txt for new files

## Algorithm Details

### A* Search
- State = (GridPt, Dir) - direction-aware for turn penalty
- Heuristic = Manhattan distance
- Max iterations = 10000
- Cost = 1 per step + turn penalty (3.0)

### Obstacles
- Each node rect expanded by ROUTE_CLEARANCE (1 grid cell)
- Convert to HashSet<GridPt>

### Path Simplification
- Remove intermediate points on straight lines
- Keep only corner points

## TDD Tests
1. GridPt conversion (world ↔ grid)
2. A* finds path through empty space
3. A* avoids obstacles
4. Path simplification
5. L-shape fallback

## Files to Create/Modify
- NEW: src/editor/router/grid.h
- NEW: src/editor/router/algorithm.h
- NEW: src/editor/router/path.h
- NEW: src/editor/router/router.h
- MOD: src/editor/app.cpp (add R key handler)
- MOD: examples/CMakeLists.txt
