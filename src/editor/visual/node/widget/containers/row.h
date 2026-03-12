#pragma once

#include "linear_layout.h"

/// Row: children laid out horizontally (left-to-right).
/// Thin alias over LinearLayout<Axis::Horizontal>.
using Row = LinearLayout<Axis::Horizontal>;
