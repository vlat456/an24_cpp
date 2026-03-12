#pragma once

#include "linear_layout.h"

/// Column: children laid out vertically (top-to-bottom).
/// Thin alias over LinearLayout<Axis::Vertical>.
using Column = LinearLayout<Axis::Vertical>;
