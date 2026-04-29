#pragma once

#include "graph.h"
#include "layer.h"

namespace bp2::layout::sugiyama {

/// Reorder nodes within each layer to minimize edge crossings.
/// Uses barycenter heuristic with bidirectional sweeps.
///
/// For each sweep (left→right then right→left):
///   For each layer:
///     Compute barycenter of each node = average position of its
///     neighbors in the adjacent layer.
///     Sort layer by barycenter (stable sort for determinism).
///
/// @param num_sweeps  Number of full left→right + right→left sweeps.
void minimize_crossings(const Graph& graph, Layering& layering, int num_sweeps = 4);

} // namespace bp2::layout::sugiyama
