#pragma once

#include "blueprint_v2/flattener/flat_netlist.h"
#include "blueprint_v2/path/path.h"
#include "json_parser/json_parser.h"
#include "ui/core/interned_id.h"

#include <nlohmann/json.hpp>

namespace bp2::elaboration {

struct SimulationExport {
    nlohmann::json devices;
    nlohmann::json connections;
};

SimulationExport to_simulation_export(
    const FlatNetlist& netlist,
    PathArena& arena,
    const ui::StringInterner& interner,
    const TypeRegistry* type_registry);

} // namespace bp2::elaboration
