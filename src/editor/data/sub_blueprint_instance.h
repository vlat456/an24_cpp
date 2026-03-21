#pragma once

#include "../../ui/math/pt.h"
#include "layout_constants.h"
#include <string>
#include <vector>
#include <map>

using ui::Pt;

/// Instance of a sub-blueprint — reference (baked_in=false) or embedded (baked_in=true).
struct SubBlueprintInstance {
    std::string id;                  // Unique instance ID: "lamp_1"
    std::string blueprint_path;      // "library/systems/lamp_pass_through.blueprint"
    std::string type_name;           // "lamp_pass_through" (for UI display)

    bool baked_in = false;           // true = inline devices saved to JSON
                                     // false = expand from library file on load

    Pt pos = Pt::zero();             // Layout of collapsed node
    Pt size = Pt(editor_constants::SUB_BLUEPRINT_DEFAULT_WIDTH, editor_constants::SUB_BLUEPRINT_DEFAULT_HEIGHT);

    std::map<std::string, std::string> params_override;
    std::map<std::string, Pt> layout_override;
    std::map<std::string, std::vector<Pt>> internal_routing;

    std::vector<std::string> internal_node_ids;

    /// Mapping: expose port name → internal node key (unprefixed).
    /// E.g. {"v" → "blueprintinput_1", "Comp" → "blueprintoutput_1"}.
    /// Built during expansion, used by to_simulator_json() for wire rewriting.
    std::map<std::string, std::string> port_to_node_key;

    SubBlueprintInstance() = default;

    SubBlueprintInstance(const std::string& id_, const std::string& path, const std::string& type)
        : id(id_), blueprint_path(path), type_name(type) {}
};
