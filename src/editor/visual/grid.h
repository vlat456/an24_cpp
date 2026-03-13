#pragma once
#include "ui/core/grid.h"

namespace visual {

class Widget;

/// visual::Grid is now a type alias for the generic ui::Grid<Widget>.
using Grid = ui::Grid<Widget>;

} // namespace visual
