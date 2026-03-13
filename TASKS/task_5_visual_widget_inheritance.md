# Task: Make visual::Widget extend ui::Widget (Phase 5) - ABANDONED

## What Was Done

1. Updated `src/editor/visual/widget.h`:
   - Changed to inherit from `ui::Widget`
   - Added includes for `ui/core/widget.h` and `visual/render_context.h`
   - Added `using ui::Pt; using ui::IDrawList;` for backward compatibility
   - Kept `scene_` and `scene_` members as protected

2. Updated `src/editor/visual/widget.cpp`:
   - Changed `renderTree` to use `ui::IDrawList` and call child methods with proper casts

3. Updated `src/editor/visual/scene.cpp`:
   - Changed sorting from `renderLayer()` to `zOrder()`

4. Updated `src/editor/visual/scene.h`:
   - Changed `render()` to use `ui::IDrawList`

5. Removed `RenderLayer` from:
   - `text_node_widget.h/cpp` - replaced with `setZOrder(1.0f)` in constructor
   - `group_node_widget.h/cpp` - replaced with `setZOrder(0.0f)` in constructor
   - `wire.h/cpp` - replaced with `setZOrder(2.0f)` in constructor

6. Updated container files:
   - `container.h` - fixed const_cast issue and render calls
   - `linear_layout.h` - fixed render calls

## Difficulty Encountered

The main issue was the **polymorphic hierarchy conflict**:

1. `ui::Widget` uses `ui::Scene*` for scene management
2. `visual::Widget` uses `visual::Scene*` for scene management  
3. These are different classes with no inheritance relationship

When trying to make `visual::Widget` inherit from `ui::Widget`:
- The `scene_` and `parent_` pointers become `ui::Scene*` and `ui::Widget*`
- But `visual::Scene` and `visual::Widget` have different APIs (grid management, etc.)
- The type system cannot reconcile these two different scene hierarchies

**Root cause**: The two widget systems have fundamentally different scene management models:
- `ui::Scene` is pure UI with generic grid
- `visual::Scene` is domain-specific with visual grid, wire crossings, etc.

## Recommendation

This task requires a more careful design. Options:
1. Keep the two widget hierarchies completely separate (current state)
2. Make `visual::Scene` inherit from `ui::Scene` - requires significant refactoring
3. Use composition instead of inheritance - make visual::Widget hold a ui::Widget pointer
4. Remove scene management from ui::Widget entirely and make it purely presentational

## Additional notes

The **ui::Scene tests are too complex** - they expect scene/grid management. Need to decide:
- Should `ui::Scene` even have grid/scene management? Or should it be pure presentation layer?
- Current `ui::Scene` has `Grid` and does grid propagation/detachment, but `ui::Widget` has no `scene_` pointer anymore

- Tests create `ui::Scene` with grid propagation expectations
- Real `ui::Scene` users won't need grid propagation - they just want to add/remove widgets

**Decision needed**: What should `ui::Scene` be `ui::Grid` actually do?
- Keep it simple: `ui::Scene` just holds widgets, delegates grid management to `ui::Grid`
- Or remove grid entirely from `ui::Scene` (make it pure presentation)
- Or simplify: `ui::Scene` doesn't need grid propagation at all
