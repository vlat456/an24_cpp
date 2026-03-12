#pragma once

#include "data/pt.h"
#include "data/blueprint.h"
#include <vector>
#include <unordered_map>

class VisualNodeCache;

namespace polyline_builder {

std::vector<std::vector<Pt>> build_wire_polylines(
    const Blueprint& bp,
    const std::string& group_id,
    VisualNodeCache& cache);

}
