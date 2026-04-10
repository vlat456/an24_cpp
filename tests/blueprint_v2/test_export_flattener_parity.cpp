#include <gtest/gtest.h>

#include "blueprint_v2/flattener/flattener.h"
#include "blueprint_v2/elaboration/sim_export.h"
#include "blueprint_v2/library/blueprint_library.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/interface/interface.h"
#include "blueprint_v2/interface/port_descriptor.h"
#include "blueprint_v2/path/path.h"
#include "ui/core/interned_id.h"

#include <set>
#include <string>

namespace {

bp2::Blueprint::Node make_node(ui::StringInterner& I,
                               const char* id,
                               const char* type,
                               std::initializer_list<bp2::PortDescriptor> ports) {
    bp2::Blueprint::Node n;
    n.semantic.id = I.intern(id);
    n.semantic.type = I.intern(type);
    n.semantic.iface = bp2::Interface(ports);
    return n;
}

bp2::PortDescriptor in_port(ui::StringInterner& I, const char* name, PortType t = PortType::V) {
    return {I.intern(name), ::domain_for_port_type(t), bp2::Direction::Input, t};
}

bp2::PortDescriptor out_port(ui::StringInterner& I, const char* name, PortType t = PortType::V) {
    return {I.intern(name), ::domain_for_port_type(t), bp2::Direction::Output, t};
}

std::set<std::string> collect_conn_edges(const nlohmann::json& connections) {
    std::set<std::string> out;
    for (const auto& c : connections) {
        out.insert(c.at("from").get<std::string>() + "->" + c.at("to").get<std::string>());
    }
    return out;
}

std::set<std::string> collect_device_names(const nlohmann::json& devices) {
    std::set<std::string> out;
    for (const auto& d : devices) {
        out.insert(d.at("name").get<std::string>());
    }
    return out;
}

} // namespace



// ==============================================================================
// 3-level nesting: root → mid(embedded) → sub(embedded) → leaf
//
// Topology:
//   root: bat ──v_out──→ [mid].vin
//   mid:  vin(bridge) ──ext──→ [sub].pin
//   sub:  pin(bridge) ──ext──→ r1.v_in
//
// Expected flat components: bat, mid, mid:vin, mid:sub, mid:sub:pin, mid:sub:r1
// Expected connections after bridge rewrite:
//   bat.v_out       → mid:vin.ext       (root-level bridge rewrite)
//   mid:vin.ext     → mid:sub:pin.ext   (mid-level bridge rewrite)
//   mid:sub:pin.ext → mid:sub:r1.v_in   (inner direct connection)
// ==============================================================================

