# Bug: Duplicate ports on AZS VerticalToggle component

## Status
🔴 **OPEN** - Created 2026-03-10

## Description
После переделки AZS из кнопки (Switch) в вертикальный тумблер (VerticalToggle), справа рисуются **два вертикальных ряда портов**:
- Один ряд - правильный (порты на своих местах)
- Второй ряд - неправильный (порт в позиции y=0)

## Expected Behavior
Должен быть только один ряд портов с каждой стороны:
- Слева - input ports (v_in, control)
- Справа - output ports (v_out, state, temp, tripped)

## Actual Behavior
Справа рисуются два ряда портов - один правильный, второй дублируется в неправильной позиции (y=0).

## Changes Made

### 1. Created VerticalToggleWidget
**Files**: `src/editor/visual/node/widget.h`, `src/editor/visual/node/widget.cpp`

Вертикальный тумблер-слайдер:
- Верхняя позиция (15% от top) = ВКЛ (зелёный)
- Нижняя позиция (70% от top) = ВЫКЛ (серый)
- Красный цвет при перегреве (thermal trip)
- Размер: 16×50 world units

### 2. Added VerticalToggle content type
**File**: `src/editor/data/node.h`

```cpp
enum class NodeContentType {
    None,
    Gauge,
    Switch,
    VerticalToggle,  // NEW
    Value,
    Text
};
```

### 3. Changed layout structure for VerticalToggle
**File**: `src/editor/visual/node/node.cpp`, `buildLayout()`

**Old structure** (Switch):
```
Header
--------
Port row 1 (label ——— spacer ——— label)
Port row 2 (label ——— spacer ——— label)
--------
[CONTENT AREA - flexible]
--------
TypeName
```

**New structure** (VerticalToggle):
```
Header
--------
Row with 3 columns:
  [Column - inputs]  [Column - toggle (flexible)]  [Column - outputs]
--------
TypeName
```

### 4. Updated buildPorts() for VerticalToggle
**File**: `src/editor/visual/node/node.cpp`

Added special handling in `buildPorts()`:
```cpp
if (node_content_.type == NodeContentType::VerticalToggle) {
    // Iterate through port_slots directly (no duplication)
    for (const auto& slot : port_slots_) {
        if (slot.is_left) {
            // Input port
        } else {
            // Output port
        }
    }
}
```

### 5. Added cache invalidation
**File**: `src/editor/visual/node/node.cpp`, `VisualNodeCache::getOrCreate()`

```cpp
// If content type changed, recreate the node
if (it->second->getContentType() != node.node_content.type) {
    auto visual = VisualNodeFactory::create(node, wires);
    auto* ptr = visual.get();
    cache_[node.id] = std::move(visual);
    return ptr;
}
```

### 6. Updated AZS JSON
**File**: `library/electrical/AZS.json`

```json
{
  "classname": "AZS",
  "content_type": "VerticalToggle",
  ...
}
```

## Current Status
- ✅ Project compiles without errors
- ✅ Toggle renders BETWEEN port labels (correct position)
- ✅ Left port labels display correctly
- ❌ **BUG**: Two rows of ports on the right side

## Investigation Checklist

### High Priority
- [ ] Check `buildPorts()` - look for duplicate code path for outputs
- [ ] Verify `ports_.clear()` is called before filling ports
- [ ] Check if standard layout code is also running for VerticalToggle

### Medium Priority
- [ ] Verify cache invalidation is working correctly
- [ ] Check `port_slots_` - verify no duplicate entries
- [ ] Add debug logging to count how many ports are created

### Low Priority
- [ ] Check render code - verify where second row comes from
- [ ] Test with clean cache (restart editor)

## Key Files to Debug

### Primary
- `src/editor/visual/node/node.cpp`
  - `buildLayout()` - lines ~130-309
  - `buildPorts()` - lines ~311-337
  - `VisualNodeCache::getOrCreate()` - lines ~903-920

### Secondary
- `src/editor/visual/node/node.h` - VisualNode class definition
- `src/editor/data/node.h` - NodeContent, NodeContentType
- `library/electrical/AZS.json` - AZS definition

### Debugging Tips
1. Add `printf("ports_.size() = %zu\n", ports_.size());` in `buildPorts()`
2. Add `printf("port_slots_.size() = %zu\n", port_slots_.size());` in `buildLayout()`
3. Check if `node_content_.type == NodeContentType::VerticalToggle` is working correctly
4. Verify that `ports_.clear()` is called at the start of `buildPorts()`

## Hypothesis

Most likely cause: The `else` block in `buildLayout()` (standard layout) might still be executing for VerticalToggle, creating duplicate port slots.

Check line ~267 in `node.cpp` - verify the `if (node_content_.type == NodeContentType::VerticalToggle)` block is exclusive and doesn't fall through to the `else` block.
