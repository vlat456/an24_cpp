# Theme Color Palette & Reskin Prompt

## The 32-Color Palette

All colors are muted, low-saturation, warm-shifted dark-room palette.
Hue family: slate-blue-gray base, amber/teal accents, no pure whites or pure saturated primaries.
Format: `Name — #RRGGBB — ImGui ABGR uint32 (0xAABBGGRR)`

### Background / Surface

| #   | Name      | #RRGGBB | ImGui uint32 | Use                             |
| --- | --------- | ------- | ------------ | ------------------------------- |
| 01  | Canvas    | #14151A | `0xFF1A1514` | OpenGL clear, canvas background |
| 02  | Surface 0 | #1C1D24 | `0xFF241D1C` | Tool panels, docked windows     |
| 03  | Surface 1 | #22232C | `0xFF2C2322` | Node body fill                  |
| 04  | Surface 2 | #292A35 | `0xFF352A29` | Node header fill                |
| 05  | Surface 3 | #30313E | `0xFF3E3130` | Bus / bar nodes                 |
| 06  | Surface 4 | #383948 | `0xFF483938` | Popup / context menu bg         |

### Borders & Structural Lines

| #   | Name          | #RRGGBB | ImGui uint32 | Use                                |
| --- | ------------- | ------- | ------------ | ---------------------------------- |
| 07  | Border Subtle | #2E2F3C | `0xFF3C2F2E` | Node default border, frame borders |
| 08  | Border Mid    | #3E4054 | `0xFF54403E` | Selected node border (not active)  |
| 09  | Grid Dot      | #252631 | `0xFF312625` | Grid lines fine step               |
| 10  | Grid Major    | #2A2B38 | `0xFF382B2A` | Grid major lines                   |

### Text

| #   | Name           | #RRGGBB | ImGui uint32 | Use                                      |
| --- | -------------- | ------- | ------------ | ---------------------------------------- |
| 11  | Text Primary   | #D4D5DC | `0xFFDCD5D4` | Node names, header text, primary UI text |
| 12  | Text Secondary | #858696 | `0xFF968685` | Port labels, type hints, dim UI text     |
| 13  | Text Disabled  | #4E4F5C | `0xFF5C4F4E` | Inactive items                           |

### Accent — Amber (interaction / selection)

| #   | Name         | #RRGGBB | ImGui uint32 | Use                                          |
| --- | ------------ | ------- | ------------ | -------------------------------------------- |
| 14  | Amber Bright | #C8922A | `0xFF2A92C8` | Selected wire, active node border, checkmark |
| 15  | Amber Mid    | #A07428 | `0xFF2874A0` | Hover highlight, slider grab                 |
| 16  | Amber Dim    | #6B4E1E | `0xFF1E4E6B` | Button bg, header hover                      |

### Accent — Teal (wires, current, action)

| #   | Name        | #RRGGBB | ImGui uint32 | Use                              |
| --- | ----------- | ------- | ------------ | -------------------------------- |
| 17  | Teal Bright | #3CA8A0 | `0xFFA0A83C` | Energized wire (current flowing) |
| 18  | Teal Mid    | #2E8078 | `0xFF78802E` | Button active, tab active        |
| 19  | Teal Dim    | #1E504C | `0xFF4C501E` | Drag-drop target tint            |

### Wires

| #   | Name           | #RRGGBB | ImGui uint32 | Use                            |
| --- | -------------- | ------- | ------------ | ------------------------------ |
| 20  | Wire Inactive  | #485060 | `0xFF605048` | Unselected wire                |
| 21  | Wire Selected  | #C8922A | `0xFF2A92C8` | Selected wire (= Amber Bright) |
| 22  | Wire Energized | #3CA8A0 | `0xFFA0A83C` | Energized wire (= Teal Bright) |
| 23  | Routing Point  | #B06020 | `0xFF2060B0` | Routing point circle           |

### Port Types (8 physical types, one color each)

| #   | Name             | #RRGGBB   | ImGui uint32 | Port type     |
| --- | ---------------- | --------- | ------------ | ------------- |
| 24  | Port Voltage     | #B85850   | `0xFF5058B8` | V (voltage)   |
| 25  | Port Current     | #506898   | `0xFF986850` | I (current)   |
| 26  | Port Bool        | #5A9060   | `0xFF60905A` | Bool          |
| 27  | Port RPM         | #B87C38   | `0xFF387CB8` | RPM           |
| 28  | Port Temperature | #A04848   | `0xFF4848A0` | Temperature   |
| 29  | Port Pressure    | `#48788C` | `0xFF8C7848` | Pressure      |
| 30  | Port Position    | #7860A8   | `0xFFA86078` | Position      |
| 31  | Port Any         | #606070   | `0xFF706060` | Any / generic |

### Gauge / Instrument

| #   | Name   | #RRGGBB | ImGui uint32 | Use                  |
| --- | ------ | ------- | ------------ | -------------------- |
| 32  | Needle | #C8702A | `0xFF2A70C8` | Gauge needle + pivot |

---

## How to Apply — Instruction for AI

### Overview of what needs changing

There are **4 files** that contain all color definitions. Change only those files.
Do NOT scatter raw `0xFF...` literals into other files — all colors must go through named constants.

---

### File 1: `src/editor/visual/renderer/render_theme.h`

This file defines `namespace render_theme { ... }` with `constexpr uint32_t COLOR_*` constants
and two inline functions: `get_node_colors()` and `get_port_color()`.

**Replace every `constexpr uint32_t COLOR_*` value** with the new palette values:

```cpp
constexpr uint32_t COLOR_TEXT         = 0xFFDCD5D4;  // Text Primary
constexpr uint32_t COLOR_TEXT_DIM     = 0xFF968685;  // Text Secondary
constexpr uint32_t COLOR_WIRE         = 0xFF2A92C8;  // Wire Selected (Amber Bright)
constexpr uint32_t COLOR_WIRE_UNSEL   = 0xFF605048;  // Wire Inactive
constexpr uint32_t COLOR_WIRE_CURRENT = 0xFFA0A83C;  // Wire Energized (Teal Bright)
constexpr uint32_t COLOR_GRID         = 0xFF312625;  // Grid Dot
constexpr uint32_t COLOR_SELECTED     = 0xFF2A92C8;  // Selected border (Amber Bright)
constexpr uint32_t COLOR_PORT_INPUT   = 0xFF986850;  // Port Current (generic input)
constexpr uint32_t COLOR_PORT_OUTPUT  = 0xFF5058B8;  // Port Voltage (generic output)
constexpr uint32_t COLOR_ROUTING_POINT= 0xFF2060B0;  // Routing Point
constexpr uint32_t COLOR_JUMP_ARC     = 0xFF605048;  // same as inactive wire
constexpr uint32_t COLOR_BODY_FILL    = 0xFF2C2322;  // Surface 1
constexpr uint32_t COLOR_HEADER_FILL  = 0xFF352A29;  // Surface 2
constexpr uint32_t COLOR_BUS_FILL     = 0xFF3E3130;  // Surface 3
constexpr uint32_t COLOR_BUS_BORDER   = 0xFF54403E;  // Border Mid
```

**Replace the entire body of `get_node_colors()`** — keep the same structure
(static map, string key lookup, same type_name keys), only change the `fill` and `border` values.
All node type colors must be desaturated/muted versions in the same hue family as before,
consistent with the Surface 0–4 scale. Use Surface 3 / Surface 4 / Border Mid as the darkest
border values. Example replacements:

```cpp
{"battery",   {0xFF3E3130, 0xFF2A2B38}},  // olive-teal tint, dark border
{"relay",     {0xFF2C3530, 0xFF1E2E2A}},  // dark teal
{"lightbulb", {0xFF3E3220, 0xFF1E2814}},  // amber-warm
{"pump",      {0xFF263040, 0xFF1A222E}},  // dark blue
{"valve",     {0xFF263040, 0xFF2A1E1A}},  // same blue, warm border
{"sensor",    {0xFF38262A, 0xFF261830}},  // rose-purple
{"subsystem", {0xFF263838, 0xFF1A2A26}},  // teal
{"motor",     {0xFF303020, 0xFF282818}},  // olive
{"generator", {0xFF2C3820, 0xFF202A28}},  // green-gray
{"switch",    {0xFF3C3220, 0xFF2A2418}},  // warm amber
{"bus",       {0xFF303140, 0xFF20222E}},  // slate
{"gyroscope", {0xFF30263A, 0xFF221630}},  // purple
{"agk47",     {0xFF3A2220, 0xFF281412}},  // dark red
{"refnode",   {0xFF222230, 0xFF141420}},  // near-black slate
```

Default fallback: `{0xFF2C2322, 0xFF1C1D24}` (Surface 1 fill, Surface 0 border).

**Replace the entire body of `get_port_color()`**:

```cpp
case an24::PortType::V:           return 0xFF5058B8;  // Port Voltage  (muted red-slate)
case an24::PortType::I:           return 0xFF986850;  // Port Current  (muted blue-teal)
case an24::PortType::Bool:        return 0xFF60905A;  // Port Bool     (muted green)
case an24::PortType::RPM:         return 0xFF387CB8;  // Port RPM      (muted orange)
case an24::PortType::Temperature: return 0xFF4848A0;  // Port Temp     (muted rose)
case an24::PortType::Pressure:    return 0xFF8C7848;  // Port Pressure (muted cyan-teal)
case an24::PortType::Position:    return 0xFFA86078;  // Port Position (muted purple)
case an24::PortType::Any:         return 0xFF706060;  // Port Any      (neutral gray)
default:                          return 0xFF706060;
```

---

### File 2: `src/editor/visual/node/widget.h`

Inside `class VoltmeterWidget`, replace the four `static constexpr uint32_t COLOR_*` lines:

```cpp
static constexpr uint32_t COLOR_GAUGE_BG     = 0xFF1C1D24;  // Surface 0
static constexpr uint32_t COLOR_GAUGE_BORDER = 0xFF3E3130;  // Surface 3
static constexpr uint32_t COLOR_NEEDLE       = 0xFF2A70C8;  // Needle (amber-orange)
static constexpr uint32_t COLOR_TICK_MAJOR   = 0xFFDCD5D4;  // Text Primary
static constexpr uint32_t COLOR_TICK_MINOR   = 0xFF606070;  // Port Any / mid gray
static constexpr uint32_t COLOR_TEXT         = 0xFFDCD5D4;  // Text Primary
```

---

### File 3: `src/editor/visual/node/widget.cpp`

There are **two hardcoded `0xFF...` literals** in this file that are not using named constants.
Replace them:

- Line with `dl->add_text(..., 0xFFFFFFFF, font)` in `HeaderWidget::render()`:
  Change `0xFFFFFFFF` to `render_theme::COLOR_TEXT`

- Line with `dl->add_text(..., 0xFFAAAAAA, font)` in `TypenameWidget::render()` (the type hint):
  Change `0xFFAAAAAA` to `render_theme::COLOR_TEXT_DIM`

- Line with `dl->add_text(..., 0xFFAAAAAA, ...)` for unit text in `VoltmeterWidget::render()`:
  Change `0xFFAAAAAA` to `render_theme::COLOR_TEXT_DIM`

You must add `#include "editor/visual/renderer/render_theme.h"` at the top of `widget.cpp`
if it is not already there. Check before adding.

---

### File 4: `src/editor/imgui_theme.cpp` — function `ApplyModernDarkTheme()`

Replace all `ImVec4(...)` color values inside `ApplyModernDarkTheme()`.
The Surface/Text/Accent color values from the palette map to ImGui floats as follows
(convert `#RRGGBB` to r/255, g/255, b/255):

| ImGuiCol              | Palette name   | ImVec4 (R,G,B,A)      |
| --------------------- | -------------- | --------------------- |
| Text                  | Text Primary   | (0.83,0.84,0.86,1)    |
| TextDisabled          | Text Disabled  | (0.31,0.31,0.36,1)    |
| WindowBg              | Surface 0      | (0.11,0.11,0.14,1)    |
| ChildBg               | — transparent  | (0,0,0,0)             |
| PopupBg               | Surface 4      | (0.22,0.22,0.28,1)    |
| Border                | Border Subtle  | (0.18,0.19,0.24,1)    |
| BorderShadow          | — transparent  | (0,0,0,0)             |
| FrameBg               | Surface 1      | (0.13,0.14,0.17,1)    |
| FrameBgHovered        | Surface 2      | (0.16,0.16,0.21,1)    |
| FrameBgActive         | Surface 3      | (0.19,0.19,0.24,1)    |
| TitleBg               | Surface 0      | (0.11,0.11,0.14,1)    |
| TitleBgActive         | Surface 2      | (0.16,0.16,0.21,1)    |
| TitleBgCollapsed      | Surface 0      | (0.11,0.11,0.14,1)    |
| MenuBarBg             | Surface 1      | (0.13,0.14,0.17,1)    |
| ScrollbarBg           | Surface 0      | (0.11,0.11,0.14,1)    |
| ScrollbarGrab         | Border Mid     | (0.24,0.25,0.33,1)    |
| ScrollbarGrabHovered  | Amber Dim      | (0.42,0.31,0.12,1)    |
| ScrollbarGrabActive   | Amber Mid      | (0.63,0.45,0.16,1)    |
| CheckMark             | Amber Bright   | (0.78,0.57,0.16,1)    |
| SliderGrab            | Amber Mid      | (0.63,0.45,0.16,1)    |
| SliderGrabActive      | Amber Bright   | (0.78,0.57,0.16,1)    |
| Button                | Surface 3      | (0.19,0.19,0.24,1)    |
| ButtonHovered         | Amber Dim      | (0.42,0.31,0.12,1)    |
| ButtonActive          | Amber Mid      | (0.63,0.45,0.16,1)    |
| Header                | Surface 3      | (0.19,0.19,0.24,1)    |
| HeaderHovered         | Amber Dim      | (0.42,0.31,0.12,1)    |
| HeaderActive          | Amber Mid      | (0.63,0.45,0.16,1)    |
| Separator             | Border Subtle  | (0.18,0.19,0.24,1)    |
| SeparatorHovered      | Amber Dim      | (0.42,0.31,0.12,1)    |
| SeparatorActive       | Amber Bright   | (0.78,0.57,0.16,1)    |
| ResizeGrip            | Border Mid     | (0.24,0.25,0.33,1)    |
| ResizeGripHovered     | Amber Mid      | (0.63,0.45,0.16,1)    |
| ResizeGripActive      | Amber Bright   | (0.78,0.57,0.16,1)    |
| Tab                   | Surface 2      | (0.16,0.16,0.21,1)    |
| TabHovered            | Amber Dim      | (0.42,0.31,0.12,1)    |
| TabActive             | Amber Mid      | (0.63,0.45,0.16,1)    |
| TabUnfocused          | Surface 1      | (0.13,0.14,0.17,1)    |
| TabUnfocusedActive    | Surface 2      | (0.16,0.16,0.21,1)    |
| PlotLines             | Text Secondary | (0.52,0.53,0.59,1)    |
| PlotLinesHovered      | Amber Bright   | (0.78,0.57,0.16,1)    |
| PlotHistogram         | Teal Mid       | (0.18,0.50,0.47,1)    |
| PlotHistogramHovered  | Teal Bright    | (0.24,0.66,0.63,1)    |
| TextSelectedBg        | Amber Dim α35  | (0.42,0.31,0.12,0.35) |
| DragDropTarget        | Amber Bright   | (0.78,0.57,0.16,0.95) |
| NavHighlight          | Amber Bright   | (0.78,0.57,0.16,1)    |
| NavWindowingHighlight | Text Primary   | (0.83,0.84,0.86,0.70) |
| NavWindowingDimBg     | — dim          | (0.10,0.10,0.12,0.20) |
| ModalWindowDimBg      | — dim          | (0.10,0.10,0.12,0.35) |

Keep all `style.*` spacing/rounding values exactly as they are — do not change them.

---

### File 5: `examples/an24_editor.cpp` — OpenGL clear color

Find the line:

```cpp
glClearColor(0.118f, 0.118f, 0.137f, 1.0f);  // RGB: 30, 30, 35
```

Replace with:

```cpp
glClearColor(0.078f, 0.082f, 0.102f, 1.0f);  // Canvas #14151A
```

---

### What NOT to change

- Do not touch any `.cpp` or `.h` file other than the 5 listed above.
- Do not change any constants that are not colors (sizes, radii, angles, font sizes, etc.).
- Do not add any new color constants — only change existing values.
- Do not change test files.
- After each file edit, verify the file compiles: run `make -j8 editor_widget_tests` from `/Users/vladimir/an24_cpp/build`.

---

### Verification

After all changes, run:

```
cd /Users/vladimir/an24_cpp/build && make -j8 && ctest --output-on-failure 2>&1 | tail -10
```

Expected: same number of tests pass as before (880 total, 1 pre-existing failure: `RouterTest.RouteWithPortDeparture`).
