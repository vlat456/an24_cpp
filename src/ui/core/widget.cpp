#include "widget.h"
#include "scene.h"

namespace ui {

// Widget is fully header-only (template).
// This file ensures BaseScene / BaseWidget are instantiated in at least one TU.
template class Widget<BaseScene>;

} // namespace ui
