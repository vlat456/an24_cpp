# Task 5b: Mechanical Fixes After Widget Inheritance Refactor

## Status: CANNOT EXECUTE - Task 5 not completed

## Reason

This task (5b) assumes that Task 5 was successfully completed, where:
- `visual::Widget` inherits from `ui::Widget`
- `visual::Scene` inherits from `ui::Scene`
- `visual::Grid` is a type alias for `ui::Grid`
- The `RenderLayer` enum has been removed from `visual::Widget`

However, Task 5 was **ABANDONED** due to a fundamental architectural conflict:

### The Problem

When attempting to make `visual::Widget` inherit from `ui::Widget`:

1. `ui::Widget` uses `ui::Scene*` for scene management
2. `visual::Widget` uses `visual::Scene*` for scene management (with different API - grid management, etc.)
3. These two scene types have no inheritance relationship

The type system cannot reconcile these two different scene hierarchies. The `scene_` and `parent_` pointers in `ui::Widget` become `ui::Scene*` and `ui::Widget*`, but the visual system needs `visual::Scene*` and `visual::Widget*`.

### What Was Tried

- Removing `scene_` and `parent_` from `ui::Widget` entirely
- Using forward declarations and friend classes
- Making `ui::Widget` purely presentational without scene management

None of these approaches worked without a complete redesign of the architecture.

## How to Proceed

To complete this task, one of these approaches is needed:

1. **Option A**: Make `visual::Scene` inherit from `ui::Scene` and `visual::Widget` inherit from `ui::Widget` - this requires significant refactoring of both scene classes to have compatible APIs

2. **Option B**: Use composition instead of inheritance - make `visual::Widget` hold a `ui::Widget` pointer rather than inherit

3. **Option C**: Keep the two widget hierarchies completely separate (current state)

4. **Option D**: Make `ui::Widget` purely presentational (remove all scene/parent/children management) and have the visual layer implement its own hierarchy

## Prerequisites

Complete Task 5 (widget inheritance) first, then Task 5b can be executed to fix the mechanical issues.
