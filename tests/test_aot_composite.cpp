#include <gtest/gtest.h>
#include "core/solvers/aot/codegen.h"
#include "core/solvers/jit/jit_solver.h"
#include "core/model/component_registry.h"
#include "blueprint_v2/flattener/flattener.h"
#include "blueprint_v2/library/blueprint_library.h"
#include "blueprint_v2/library/type_def_to_blueprint.h"
#include "blueprint_v2/elaboration/sim_export.h"
#include "ui/core/interned_id.h"
#include "jit_build_input_test_helper.h"
#include <regex>
#include <set>
#include <unordered_map>

// ============================================================
// Composite Systems generation
// ============================================================

TEST(AotComposite, GeneratesSystemsForComposite) {
    ComponentRegistry registry;
    registry.register_type("IndicatorLight", *as_primitive(*test_registry().get("IndicatorLight")));

    CompositeSpec lamp;
    lamp.classname = "voltage_indicator";
    DeviceInstance d_lamp;
    d_lamp.name = "lamp";
    d_lamp.classname = "IndicatorLight";
    lamp.devices.push_back(d_lamp);
    lamp.bridge_ports = {
        make_bridge_port_def("vin", bp2::BridgeDirection::Input, PortType::V),
        make_bridge_port_def("vout", bp2::BridgeDirection::Output, PortType::V),
    };
    lamp.connections = {{"vin.port", "lamp.v_in", {}}, {"lamp.v_out", "vout.port", {}}};
    registry.register_type("voltage_indicator", lamp);

    // Generate code
    auto result = CodeGen::generate_composite_systems(lamp, registry);

    // Should produce header + source
    EXPECT_FALSE(result.header.empty());
    EXPECT_FALSE(result.source.empty());

    // Header should contain class name
    EXPECT_NE(result.header.find("voltage_indicator_Systems"), std::string::npos);

    // Bridge nodes are elaboration-only and must be lowered before runtime codegen.
    // AOT runtime fields should contain only simulation components.
    EXPECT_EQ(result.header.find("BridgePort"), std::string::npos);
    EXPECT_NE(result.header.find("IndicatorLight"), std::string::npos);

    // Should have solve_step and pre_load
    EXPECT_NE(result.header.find("solve_step"), std::string::npos);
    EXPECT_NE(result.header.find("pre_load"), std::string::npos);
}

TEST(AotComposite, NestedComposite_ContainsSubSystems) {
    ComponentRegistry registry;
    register_from_library(registry, {"ElectricalSource", "Bus"});

    CompositeSpec inner;
    inner.classname = "battery_wrapper";
    DeviceInstance d_bat;
    d_bat.name = "bat";
    d_bat.classname = "ElectricalSource";
    inner.devices.push_back(d_bat);
    registry.register_type("battery_wrapper", inner);

    // Outer composite references inner
    CompositeSpec outer;
    outer.classname = "battery_bank";
    SubBlueprintRef ref;
    ref.id = "sb_1";
    ref.type_name = "battery_wrapper";
    outer.sub_blueprints.push_back(ref);
    DeviceInstance d_bus;
    d_bus.name = "bus";
    d_bus.classname = "Bus";
    
    outer.devices.push_back(d_bus);
    registry.register_type("battery_bank", outer);

    auto result = CodeGen::generate_composite_systems(outer, registry);

    // Composites are FLATTENED: sub-blueprint devices get prefixed names
    // sb_1:bat → sanitized to sb_1_bat (ElectricalSource device from inner composite)
    EXPECT_NE(result.header.find("sb_1_bat"), std::string::npos)
        << "Flattened sub-blueprint device should appear with prefixed name";

    // Should also have the top-level primitive
    EXPECT_NE(result.header.find("Bus"), std::string::npos);
}

TEST(AotComposite, ThreeLevelsDeep_FullHierarchy) {
    ComponentRegistry registry;
    registry.register_type("Resistor", *as_primitive(*test_registry().get("Resistor")));

    CompositeSpec leaf;
    leaf.classname = "leaf_type";
    DeviceInstance d_r;
    d_r.name = "r1";
    d_r.classname = "Resistor";
    
    leaf.devices.push_back(d_r);
    registry.register_type("leaf_type", leaf);

    // Level 1: mid references leaf
    CompositeSpec mid;
    mid.classname = "mid_type";
    SubBlueprintRef ref_leaf;
    ref_leaf.id = "leaf_inst";
    ref_leaf.type_name = "leaf_type";
    mid.sub_blueprints.push_back(ref_leaf);
    registry.register_type("mid_type", mid);

    // Level 0: top references mid
    CompositeSpec top;
    top.classname = "top_type";
    SubBlueprintRef ref_mid;
    ref_mid.id = "mid_inst";
    ref_mid.type_name = "mid_type";
    top.sub_blueprints.push_back(ref_mid);
    registry.register_type("top_type", top);

    auto result = CodeGen::generate_composite_systems(top, registry);

    // Composites are FULLY FLATTENED: top → mid_inst:leaf_inst:r1
    // sanitized to mid_inst_leaf_inst_r1
    EXPECT_NE(result.header.find("mid_inst_leaf_inst_r1"), std::string::npos)
        << "Three-level nested device should be fully flattened with prefixed name";
    // The Resistor component type should appear
    EXPECT_NE(result.header.find("Resistor"), std::string::npos);
}

// ============================================================
// Topological ordering
// ============================================================

TEST(AotComposite, TopoSort_LeavesFirst) {
    ComponentRegistry registry;

    CompositeSpec leaf;
    leaf.classname = "leaf";
    DeviceInstance d;
    d.name = "d";
    d.classname = "ElectricalSource";
    
    leaf.devices.push_back(d);
    registry.register_type("leaf", leaf);

    CompositeSpec parent;
    parent.classname = "parent";
    SubBlueprintRef ref;
    ref.id = "l1";
    ref.type_name = "leaf";
    parent.sub_blueprints.push_back(ref);
    registry.register_type("parent", parent);

    auto order = registry.get_composites_topo_sorted();

    // leaf must come before parent
    auto it_leaf = std::find(order.begin(), order.end(), "leaf");
    auto it_parent = std::find(order.begin(), order.end(), "parent");
    ASSERT_NE(it_leaf, order.end());
    ASSERT_NE(it_parent, order.end());
    EXPECT_LT(std::distance(order.begin(), it_leaf),
              std::distance(order.begin(), it_parent));
}

TEST(AotComposite, TopoSort_MissingSubBlueprintThrows) {
    ComponentRegistry registry;

    CompositeSpec parent;
    parent.classname = "parent";
    parent.sub_blueprints.push_back(SubBlueprintRef{"missing_ref", "", "missing_type"});
    registry.register_type("parent", parent);

    EXPECT_THROW(registry.get_composites_topo_sorted(), std::runtime_error);
}

TEST(AotComposite, PreLoad_CallsSubComposites) {
    ComponentRegistry registry;
    register_from_library(registry, {"ElectricalSource", "Bus"});

    CompositeSpec inner;
    inner.classname = "inner_type";
    DeviceInstance d_bat;
    d_bat.name = "bat";
    d_bat.classname = "ElectricalSource";
    
    inner.devices.push_back(d_bat);
    registry.register_type("inner_type", inner);

    CompositeSpec outer;
    outer.classname = "outer_type";
    SubBlueprintRef ref;
    ref.id = "inner_inst";
    ref.type_name = "inner_type";
    outer.sub_blueprints.push_back(ref);
    DeviceInstance d_bus;
    d_bus.name = "bus";
    d_bus.classname = "Bus";
    
    outer.devices.push_back(d_bus);
    registry.register_type("outer_type", outer);

    auto result = CodeGen::generate_composite_systems(outer, registry);

    // Flattened: inner_inst:bat → inner_inst_bat.pre_load()
    EXPECT_NE(result.source.find("inner_inst_bat.pre_load()"), std::string::npos)
        << "pre_load() must call flattened sub-blueprint device pre_load()";
    EXPECT_NE(result.source.find("bus.pre_load()"), std::string::npos)
        << "pre_load() must call primitive device pre_load()";
}

// ============================================================
// JIT vs AOT equivalence for composites
// ============================================================
// DISABLED: legacy solver-specific test checking old bridge alias unification.
// In push model, alias semantics differ (no union-find collapsing), so JIT
// signal counts and port-to-signal mappings differ from AOT codegen.

TEST(AotComposite, OutputMatchesJitExpansion) {
    // Verify that AOT codegen and JIT produce identical signal topologies
    // for the same composite type definition.
    //
    // We can't compile/run AOT C++ at test time, but we CAN verify that
    // both paths expand the same devices, allocate the same signal count,
    // and wire the same port names to the same signal equivalence classes.

    // ---- Build a registry with full type definitions (ports + params) ----

    ComponentRegistry registry;
    register_from_library(registry, {"IndicatorLight"});

    // Composite: voltage_indicator (vin→lamp→vout)
    CompositeSpec lamp;
    lamp.classname = "voltage_indicator";

    DeviceInstance d_lamp;
    d_lamp.name = "lamp";
    d_lamp.classname = "IndicatorLight";
    d_lamp.params["conductance"] = "0.002";  // Required param for ConductanceBranch solver role
    lamp.devices.push_back(d_lamp);
    lamp.bridge_ports = {
        make_bridge_port_def("vin", bp2::BridgeDirection::Input, PortType::V),
        make_bridge_port_def("vout", bp2::BridgeDirection::Output, PortType::V),
    };
    lamp.connections = {
        {"vin.port", "lamp.v_in", {}},
        {"lamp.v_out", "vout.port", {}}
    };
    lamp.ports["vin"]  = Port{bp2::Direction::Input, PortType::V, std::nullopt};
    lamp.ports["vout"] = Port{bp2::Direction::Output, PortType::V, std::nullopt};
    registry.register_type("voltage_indicator", lamp);

    // ---- AOT path: generate_composite_systems ----

    auto aot_result = CodeGen::generate_composite_systems(lamp, registry);
    ASSERT_FALSE(aot_result.header.empty());
    ASSERT_FALSE(aot_result.source.empty());

    // Extract SIGNAL_COUNT from generated header
    std::regex signal_count_re(R"(SIGNAL_COUNT\s*=\s*(\d+))");
    std::smatch match;
    ASSERT_TRUE(std::regex_search(aot_result.header, match, signal_count_re))
        << "AOT header should contain SIGNAL_COUNT";
    uint32_t aot_signal_count = static_cast<uint32_t>(std::stoul(match[1].str()));

    // ---- JIT path: Flattener path ----

    ui::StringInterner jit_interner;
    bp2::BlueprintLibrary jit_library;
    for (const auto& [name, spec] : registry.all_types()) {
        if (is_composite(spec)) {
            auto bp = bp2::blueprint_from_type_definition(spec, jit_interner, registry);
            jit_library.add(jit_interner.intern(name), std::move(bp));
        }
    }
    auto jit_bp = bp2::blueprint_from_type_definition(ComponentSpec{lamp}, jit_interner, registry);
    bp2::PathArena jit_arena(jit_interner);
    bp2::Flattener jit_flattener(jit_library);
    auto jit_netlist = jit_flattener.flatten(jit_bp, jit_arena);
    auto jit_input = bp2::elaboration::elaborate_for_jit(jit_netlist, jit_arena, jit_interner, registry);
    BuildResult jit_result = build_systems_dev(jit_input);

    // ---- Compare signal topologies ----

    // Both paths now use the same signal allocation source via composite helpers.
    uint32_t jit_signal_count = jit_result.signal_count;
    EXPECT_EQ(jit_signal_count, aot_signal_count)
        << "JIT and AOT signal counts must match exactly (same allocation source)";

    // Runtime build/codegen lower bridge nodes before component instantiation.
    // Compare device sets from elaboration vs build (bridges excluded).
    std::set<std::string> elaborated_device_names;
    for (const auto& dev : jit_input.devices) {
        elaborated_device_names.insert(dev.name);
    }
    std::set<std::string> built_device_names;
    for (const auto& [name, _] : jit_result.devices) {
        built_device_names.insert(name);
    }
    EXPECT_EQ(elaborated_device_names, built_device_names)
        << "Elaboration and build must produce the same device names";

    // Verify connected ports land on the same signal (equivalence class check).
    // vin.port and lamp.v_in should share a signal (they are connected).
    // lamp.v_out and vout.port should share a signal.
    auto jit_sig = [&](const std::string& port) -> uint32_t {
        auto it = jit_result.port_to_signal.find(jit_result.signal_key_interner.lookup(port));
        EXPECT_NE(it, jit_result.port_to_signal.end()) << port << " should exist in JIT map";
        return it != jit_result.port_to_signal.end() ? it->second : UINT32_MAX;
    };

    // Connection: vin.port -> lamp.v_in (these should be unified)
    EXPECT_EQ(jit_sig("vin.port"), jit_sig("lamp.v_in"))
        << "Connected ports vin.port and lamp.v_in should share a signal";

    // Connection: lamp.v_out -> vout.port
    EXPECT_EQ(jit_sig("lamp.v_out"), jit_sig("vout.port"))
        << "Connected ports lamp.v_out and vout.port should share a signal";

    // Alias mapping strategy is implementation-defined in push path.
    if (jit_result.port_to_signal.count(jit_result.signal_key_interner.lookup("vin.ext")) && jit_result.port_to_signal.count(jit_result.signal_key_interner.lookup("vin.port"))) {
        EXPECT_NE(jit_sig("vin.ext"), UINT32_MAX);
        EXPECT_NE(jit_sig("vin.port"), UINT32_MAX);
    }
    if (jit_result.port_to_signal.count(jit_result.signal_key_interner.lookup("vout.ext")) && jit_result.port_to_signal.count(jit_result.signal_key_interner.lookup("vout.port"))) {
        EXPECT_NE(jit_sig("vout.ext"), UINT32_MAX);
        EXPECT_NE(jit_sig("vout.port"), UINT32_MAX);
    }

    // Disconnected port: lamp.brightness should have its own signal
    EXPECT_NE(jit_sig("lamp.brightness"), jit_sig("vin.port"))
        << "Disconnected port lamp.brightness should NOT share signal with vin.port";
    EXPECT_NE(jit_sig("lamp.brightness"), jit_sig("lamp.v_out"))
        << "Disconnected port lamp.brightness should NOT share signal with lamp.v_out";

    // Verify runtime codegen references simulation devices and omits bridge devices.
    EXPECT_EQ(aot_result.header.find("vin"), std::string::npos)
        << "AOT header should not reference lowered bridge device 'vin'";
    EXPECT_NE(aot_result.header.find("lamp"), std::string::npos)
        << "AOT header should reference device 'lamp'";
    EXPECT_EQ(aot_result.header.find("vout"), std::string::npos)
        << "AOT header should not reference lowered bridge device 'vout'";

    // Verify AOT source contains execute calls only for runtime devices.
    EXPECT_EQ(aot_result.source.find("vin.execute"), std::string::npos)
        << "AOT source should not contain vin.execute call";
    EXPECT_NE(aot_result.source.find("lamp.execute"), std::string::npos)
        << "AOT source should contain lamp.execute call";
    EXPECT_EQ(aot_result.source.find("vout.execute"), std::string::npos)
        << "AOT source should not contain vout.execute call";
}

// ============================================================
// Electrical Plan codegen
// ============================================================

TEST(AotComposite, ElectricalPlan_BatteryAndResistor_GeneratesIslandArrays) {
    ComponentRegistry registry;
    register_from_library(registry, {"Generator", "Resistor", "RefNode"});

    // Simple circuit: ElectricalSource -> Resistor -> RefNode (fixed voltage)
    CompositeSpec circuit;
    circuit.classname = "simple_circuit";

    DeviceInstance d_bat;
    d_bat.name = "bat";
    d_bat.classname = "Generator";
    
    d_bat.params["v_nominal"] = "28.0";
    d_bat.params["internal_r"] = "0.01";

    DeviceInstance d_res;
    d_res.name = "res";
    d_res.classname = "Resistor";
    
    d_res.params["conductance"] = "10.0";

    DeviceInstance d_ref;
    d_ref.name = "gnd";
    d_ref.classname = "RefNode";
    
    d_ref.params["value"] = "0.0";

    circuit.devices.push_back(d_bat);
    circuit.devices.push_back(d_res);
    circuit.devices.push_back(d_ref);
    circuit.connections = {
        {"bat.v_out", "res.v_in", {}},
        {"res.v_out", "gnd.v", {}}
    };
    registry.register_type("simple_circuit", circuit);

    auto result = CodeGen::generate_composite_systems(circuit, registry);

    EXPECT_FALSE(result.header.empty());
    EXPECT_FALSE(result.source.empty());

    EXPECT_NE(result.header.find("ELECTRICAL_ISLAND_COUNT"), std::string::npos)
        << "Header should contain ELECTRICAL_ISLAND_COUNT";

    EXPECT_NE(result.header.find("island_0_nodes"), std::string::npos)
        << "Header should contain island_0_nodes array";

    EXPECT_NE(result.header.find("island_0_elements"), std::string::npos)
        << "Header should contain island_0_elements array";

    EXPECT_NE(result.source.find("solve_electrical"), std::string::npos)
        << "Source should contain solve_electrical call";

    EXPECT_NE(result.header.find("AotElectricalPlan"), std::string::npos)
        << "Header should contain AotElectricalPlan struct";

    EXPECT_NE(result.header.find("electrical_rt_"), std::string::npos)
        << "Header should contain electrical_rt_ member";
}

TEST(AotComposite, ElectricalPlan_IndicatorLight_GeneratesConductanceBranch) {
    ComponentRegistry registry;
    register_from_library(registry, {"ElectricalSource", "IndicatorLight"});

    CompositeSpec lamp_circuit;
    lamp_circuit.classname = "lamp_circuit";

    DeviceInstance d_bat;
    d_bat.name = "bat";
    d_bat.classname = "ElectricalSource";
    

    DeviceInstance d_lamp;
    d_lamp.name = "lamp";
    d_lamp.classname = "IndicatorLight";
    

    lamp_circuit.devices.push_back(d_bat);
    lamp_circuit.devices.push_back(d_lamp);
    lamp_circuit.connections = {
        {"bat.v_out", "lamp.v_in", {}},
        {"bat.v_in", "lamp.v_out", {}}
    };
    registry.register_type("lamp_circuit", lamp_circuit);

    auto result = CodeGen::generate_composite_systems(lamp_circuit, registry);

    EXPECT_NE(result.header.find("ELECTRICAL_ISLAND_COUNT"), std::string::npos)
        << "Header should contain ELECTRICAL_ISLAND_COUNT even for IndicatorLight";

    EXPECT_NE(result.source.find("solve_electrical"), std::string::npos)
        << "Source should contain solve_electrical for circuit with IndicatorLight";
}

TEST(AotComposite, ElectricalPlan_NoElectricalDevices_HasZeroIslands) {
    ComponentRegistry registry;
    // Bus is visual_only via TypePresentation, not PrimitiveSpec
    registry.register_type("Bus", *as_primitive(*test_registry().get("Bus")));

    CompositeSpec no_elec;
    no_elec.classname = "no_electrical";

    DeviceInstance d_bus;
    d_bus.name = "bus";
    d_bus.classname = "Bus";
    

    no_elec.devices.push_back(d_bus);
    registry.register_type("no_electrical", no_elec);

    auto result = CodeGen::generate_composite_systems(no_elec, registry);

    EXPECT_NE(result.header.find("ELECTRICAL_ISLAND_COUNT = 0"), std::string::npos)
        << "Header should show ELECTRICAL_ISLAND_COUNT = 0 when no electrical devices";

    EXPECT_EQ(result.source.find("solve_electrical"), std::string::npos)
        << "Source should NOT contain solve_electrical when no electrical devices";
}

TEST(AotComposite, ElectricalBindings_WrapperHandlesGenerated) {
    ComponentRegistry registry;
    register_from_library(registry, {"Generator", "CurrentSense", "IndicatorLight", "RefNode"});

    CompositeSpec circuit;
    circuit.classname = "wrapper_binding_circuit";

    DeviceInstance d_bat;
    d_bat.name = "bat";
    d_bat.classname = "Generator";
    

    DeviceInstance d_sense;
    d_sense.name = "sense";
    d_sense.classname = "CurrentSense";
    

    DeviceInstance d_lamp;
    d_lamp.name = "lamp";
    d_lamp.classname = "IndicatorLight";
    

    DeviceInstance d_ref;
    d_ref.name = "gnd";
    d_ref.classname = "RefNode";
    

    circuit.devices.push_back(d_bat);
    circuit.devices.push_back(d_sense);
    circuit.devices.push_back(d_lamp);
    circuit.devices.push_back(d_ref);
    circuit.connections = {
        {"bat.v_out", "sense.v_in", {}},
        {"sense.v_out", "lamp.v_in", {}},
        {"lamp.v_out", "gnd.v", {}},
        {"bat.v_in", "gnd.v", {}}
    };
    registry.register_type("wrapper_binding_circuit", circuit);

    auto result = CodeGen::generate_composite_systems(circuit, registry);

    EXPECT_NE(result.header.find("struct ElectricalBindings"), std::string::npos);
    EXPECT_NE(result.header.find("sense_element_id"), std::string::npos);
    EXPECT_NE(result.header.find("lamp_element_id"), std::string::npos);

    EXPECT_NE(result.source.find("sense.electrical_handle.element_id = ElectricalBindings::sense_element_id"), std::string::npos);
    EXPECT_NE(result.source.find("lamp.electrical_handle.element_id = ElectricalBindings::lamp_element_id"), std::string::npos);
}

TEST(AotComposite, ElectricalBindings_StableAcrossConnectionReordering) {
    ComponentRegistry registry;
    register_from_library(registry, {"Generator", "CurrentSense", "RefNode"});

    auto make_circuit = [&](const std::vector<Connection>& conns, const std::string& name) {
        CompositeSpec td;
        td.classname = name;

        DeviceInstance d_bat;
        d_bat.name = "bat";
        d_bat.classname = "Generator";
        

        DeviceInstance d_sense;
        d_sense.name = "sense";
        d_sense.classname = "CurrentSense";
        

        DeviceInstance d_ref;
        d_ref.name = "gnd";
        d_ref.classname = "RefNode";
        

        td.devices.push_back(d_bat);
        td.devices.push_back(d_sense);
        td.devices.push_back(d_ref);
        td.connections = conns;
        return td;
    };

    auto c1 = make_circuit({
        {"bat.v_out", "sense.v_in", {}},
        {"sense.v_out", "gnd.v", {}},
        {"bat.v_in", "gnd.v", {}}
    }, "bind_order_1");

    auto c2 = make_circuit({
        {"bat.v_in", "gnd.v", {}},
        {"sense.v_out", "gnd.v", {}},
        {"bat.v_out", "sense.v_in", {}}
    }, "bind_order_2");

    registry.register_type(c1.classname, c1);
    registry.register_type(c2.classname, c2);

    auto r1 = CodeGen::generate_composite_systems(c1, registry);
    auto r2 = CodeGen::generate_composite_systems(c2, registry);

    auto extract_component_const = [](const std::string& header, const std::string& dev) -> int {
        std::regex re("static constexpr uint32_t " + dev + "_element_id\\s*=\\s*(\\d+)");
        std::smatch m;
        if (!std::regex_search(header, m, re)) {
            return -1;
        }
        return std::stoi(m[1].str());
    };

    int sense_1 = extract_component_const(r1.header, "sense");
    int sense_2 = extract_component_const(r2.header, "sense");

    ASSERT_GE(sense_1, 0);
    ASSERT_GE(sense_2, 0);

    EXPECT_EQ(sense_1, sense_2);

    EXPECT_NE(r1.source.find("sense.electrical_handle.element_id = ElectricalBindings::sense_element_id"), std::string::npos);
    EXPECT_NE(r2.source.find("sense.electrical_handle.element_id = ElectricalBindings::sense_element_id"), std::string::npos);
}

TEST(AotComposite, ElectricalBindings_AssignAllHandleFieldsFromConstants) {
    ComponentRegistry registry;
    register_from_library(registry, {"Generator", "CurrentSense", "RefNode"});

    CompositeSpec circuit;
    circuit.classname = "binding_fields_circuit";

    DeviceInstance d_bat;
    d_bat.name = "bat";
    d_bat.classname = "Generator";
    

    DeviceInstance d_sense;
    d_sense.name = "sense";
    d_sense.classname = "CurrentSense";
    

    DeviceInstance d_ref;
    d_ref.name = "gnd";
    d_ref.classname = "RefNode";
    

    circuit.devices.push_back(d_bat);
    circuit.devices.push_back(d_sense);
    circuit.devices.push_back(d_ref);
    circuit.connections = {
        {"bat.v_out", "sense.v_in", {}},
        {"sense.v_out", "gnd.v", {}},
        {"bat.v_in", "gnd.v", {}}
    };
    registry.register_type("binding_fields_circuit", circuit);

    auto result = CodeGen::generate_composite_systems(circuit, registry);

    EXPECT_NE(result.header.find("bat_island"), std::string::npos);
    EXPECT_NE(result.header.find("bat_element"), std::string::npos);
    EXPECT_NE(result.header.find("bat_element_id"), std::string::npos);
    EXPECT_NE(result.header.find("sense_island"), std::string::npos);
    EXPECT_NE(result.header.find("sense_element"), std::string::npos);
    EXPECT_NE(result.header.find("sense_element_id"), std::string::npos);

    EXPECT_NE(result.source.find("bat.electrical_handle.island_index = ElectricalBindings::bat_island"), std::string::npos);
    EXPECT_NE(result.source.find("bat.electrical_handle.element_index = ElectricalBindings::bat_element"), std::string::npos);
    EXPECT_NE(result.source.find("bat.electrical_handle.element_id = ElectricalBindings::bat_element_id"), std::string::npos);
    EXPECT_NE(result.source.find("sense.electrical_handle.island_index = ElectricalBindings::sense_island"), std::string::npos);
    EXPECT_NE(result.source.find("sense.electrical_handle.element_index = ElectricalBindings::sense_element"), std::string::npos);
    EXPECT_NE(result.source.find("sense.electrical_handle.element_id = ElectricalBindings::sense_element_id"), std::string::npos);
}

// Regression: when non-electrical devices are interleaved between electrical ones,
// binding construction must still map to the correct device name (not devices[element_idx]).
TEST(AotComposite, ElectricalBindings_MixedDevicesCorrectMapping) {
    ComponentRegistry registry;
    register_from_library(registry, {"Generator", "Any_V_to_Bool", "CurrentSense", "RefNode"});

    CompositeSpec circuit;
    circuit.classname = "mixed_device_circuit";

    DeviceInstance d_bat;
    d_bat.name = "bat";
    d_bat.classname = "Generator";
    

    // Non-electrical device inserted between bat and sense
    DeviceInstance d_logic;
    d_logic.name = "logic";
    d_logic.classname = "Any_V_to_Bool";
    

    DeviceInstance d_sense;
    d_sense.name = "sense";
    d_sense.classname = "CurrentSense";
    

    DeviceInstance d_ref;
    d_ref.name = "gnd";
    d_ref.classname = "RefNode";
    

    // Key: logic device sits at index 1 in devices array, but is NOT electrical.
    // If binding code used devices[element_idx], sense would wrongly get logic's name.
    circuit.devices.push_back(d_bat);
    circuit.devices.push_back(d_logic);
    circuit.devices.push_back(d_sense);
    circuit.devices.push_back(d_ref);
    circuit.connections = {
        {"bat.v_out", "sense.v_in", {}},
        {"sense.v_out", "gnd.v", {}},
        {"bat.v_in", "gnd.v", {}},
        {"bat.v_out", "logic.Vin", {}}
    };
    registry.register_type("mixed_device_circuit", circuit);

    auto result = CodeGen::generate_composite_systems(circuit, registry);

    // bat and sense must have correct bindings (not logic's name)
    EXPECT_NE(result.header.find("bat_element_id"), std::string::npos)
        << "Battery binding missing from header";
    EXPECT_NE(result.header.find("sense_element_id"), std::string::npos)
        << "CurrentSense binding missing from header";

    // logic must NOT appear in electrical bindings (it's not an electrical component)
    EXPECT_EQ(result.header.find("logic_element_id"), std::string::npos)
        << "Non-electrical device should not have electrical binding";

    // Constructor must assign handle to bat and sense, not to logic
    EXPECT_NE(result.source.find("bat.electrical_handle.element_id = ElectricalBindings::bat_element_id"), std::string::npos)
        << "Battery handle assignment missing";
    EXPECT_NE(result.source.find("sense.electrical_handle.element_id = ElectricalBindings::sense_element_id"), std::string::npos)
        << "CurrentSense handle assignment missing";
    EXPECT_EQ(result.source.find("logic.electrical_handle"), std::string::npos)
        << "Non-electrical device should not get electrical_handle assignment";
}

TEST(AotComposite, ElectricalDebugMap_ContainsRoleAndEndpoints) {
    ComponentRegistry registry;
    register_from_library(registry, {"ElectricalSource", "CurrentSense", "RefNode"});

    CompositeSpec circuit;
    circuit.classname = "debug_map_circuit";

    DeviceInstance d_bat;
    d_bat.name = "bat";
    d_bat.classname = "ElectricalSource";
    

    DeviceInstance d_sense;
    d_sense.name = "sense";
    d_sense.classname = "CurrentSense";
    

    DeviceInstance d_ref;
    d_ref.name = "gnd";
    d_ref.classname = "RefNode";
    

    circuit.devices.push_back(d_bat);
    circuit.devices.push_back(d_sense);
    circuit.devices.push_back(d_ref);
    circuit.connections = {
        {"bat.v_out", "sense.v_in", {}},
        {"sense.v_out", "gnd.v", {}},
        {"bat.v_in", "gnd.v", {}}
    };
    registry.register_type("debug_map_circuit", circuit);

    auto result = CodeGen::generate_composite_systems(circuit, registry);

    EXPECT_NE(result.header.find("struct ElectricalDebugEntry"), std::string::npos);
    EXPECT_NE(result.header.find("ELECTRICAL_DEBUG_MAP"), std::string::npos);
    EXPECT_NE(result.header.find("ELECTRICAL_DEBUG_COUNT"), std::string::npos);

    EXPECT_NE(result.header.find("\"bat\""), std::string::npos);
    EXPECT_NE(result.header.find("\"sense\""), std::string::npos);
    EXPECT_NE(result.header.find("\"ElectricalSource\""), std::string::npos);
    EXPECT_NE(result.header.find("\"CurrentSense\""), std::string::npos);
    EXPECT_NE(result.header.find("\"TheveninSource\""), std::string::npos);
    EXPECT_NE(result.header.find("\"ConductanceBranch\""), std::string::npos);
}

TEST(AotComposite, ElectricalDiagnostics_WarnPathGenerated) {
    ComponentRegistry registry;
    register_from_library(registry, {"ElectricalSource", "ElectricalConductance", "RefNode"});

    CompositeSpec circuit;
    circuit.classname = "diag_warn_circuit";

    DeviceInstance d_src;
    d_src.name = "src";
    d_src.classname = "ElectricalSource";
    

    DeviceInstance d_load;
    d_load.name = "load";
    d_load.classname = "ElectricalConductance";
    

    DeviceInstance d_ref;
    d_ref.name = "gnd";
    d_ref.classname = "RefNode";
    

    circuit.devices.push_back(d_src);
    circuit.devices.push_back(d_load);
    circuit.devices.push_back(d_ref);
    circuit.connections = {
        {"src.v_out", "load.v_in", {}},
        {"load.v_out", "gnd.v", {}},
        {"src.v_in", "gnd.v", {}}
    };
    registry.register_type("diag_warn_circuit", circuit);

    auto result = CodeGen::generate_composite_systems(circuit, registry);

    EXPECT_NE(result.source.find("ELECTRICAL_DIAG_RESIDUAL_WARN"), std::string::npos);
    EXPECT_NE(result.source.find("electrical_rt_.island_diagnostics"), std::string::npos);
    EXPECT_NE(result.source.find("[aot-elec] island="), std::string::npos);
    EXPECT_NE(result.source.find("dump_island_debug(diag.island_index)"), std::string::npos);
    EXPECT_NE(result.source.find("[aot-elec] solve counters:"), std::string::npos);
    EXPECT_NE(result.source.find("electrical_rt_.counters"), std::string::npos);
    EXPECT_NE(result.source.find("ELECTRICAL_DEBUG_COUNT"), std::string::npos);
    EXPECT_NE(result.source.find("ELECTRICAL_DEBUG_MAP"), std::string::npos);
}

TEST(AotComposite, ElectricalDebugMap_ContainsIslandAndElementIndices) {
    ComponentRegistry registry;
    register_from_library(registry, {"ElectricalSource", "ElectricalConductance", "RefNode"});

    CompositeSpec circuit;
    circuit.classname = "debug_idx_circuit";

    DeviceInstance d_src;
    d_src.name = "src";
    d_src.classname = "ElectricalSource";
    

    DeviceInstance d_load;
    d_load.name = "load";
    d_load.classname = "ElectricalConductance";
    

    DeviceInstance d_ref;
    d_ref.name = "gnd";
    d_ref.classname = "RefNode";
    

    circuit.devices.push_back(d_src);
    circuit.devices.push_back(d_load);
    circuit.devices.push_back(d_ref);
    circuit.connections = {
        {"src.v_out", "load.v_in", {}},
        {"load.v_out", "gnd.v", {}},
        {"src.v_in", "gnd.v", {}}
    };
    registry.register_type("debug_idx_circuit", circuit);

    auto result = CodeGen::generate_composite_systems(circuit, registry);

    EXPECT_NE(result.header.find("island_index"), std::string::npos);
    EXPECT_NE(result.header.find("element_index"), std::string::npos);
    EXPECT_NE(result.source.find("if (e.island_index == island_idx)"), std::string::npos);
    EXPECT_NE(result.source.find("[aot-elec]   elem={} comp={}"), std::string::npos);
}

// =============================================================================
// Regression: Generated step_N() methods must call .commit() on devices.
// Previously codegen only emitted .execute() calls, breaking battery discharge,
// switch toggling, relay actuation, and AZS circuit breakers in AOT mode.
// =============================================================================
TEST(AotComposite, GeneratedStepMethodsIncludeCommitCalls) {
    ComponentRegistry registry;

    PrimitiveSpec battery_type;
    battery_type.classname = "ElectricalSource";
    battery_type.ports["v_out"] = Port{bp2::Direction::Output, PortType::V, std::nullopt};
    battery_type.ports["v_in"] = Port{bp2::Direction::Input, PortType::V, std::nullopt};
    battery_type.domains = {Domain::Electrical};
    registry.register_type("ElectricalSource", battery_type);

    PrimitiveSpec resistor_type;
    resistor_type.classname = "Resistor";
    resistor_type.ports["v_in"] = Port{bp2::Direction::Input, PortType::V, std::nullopt};
    resistor_type.ports["v_out"] = Port{bp2::Direction::Output, PortType::V, std::nullopt};
    resistor_type.domains = {Domain::Electrical};
    registry.register_type("Resistor", resistor_type);

    PrimitiveSpec ref_type;
    ref_type.classname = "RefNode";
    ref_type.ports["v"] = Port{bp2::Direction::InOut, PortType::V, std::nullopt};
    ref_type.domains = {Domain::Electrical};
    ref_type.solver.scheduler_source = true;
    registry.register_type("RefNode", ref_type);

    CompositeSpec circuit;
    circuit.classname = "test_circuit";

    DeviceInstance d_bat;
    d_bat.name = "bat";
    d_bat.classname = "ElectricalSource";
    d_bat.ports = {{"v_in", Port{}}, {"v_out", Port{}}};

    DeviceInstance d_load;
    d_load.name = "load";
    d_load.classname = "Resistor";
    d_load.ports = {{"v_in", Port{}}, {"v_out", Port{}}};

    DeviceInstance d_ref;
    d_ref.name = "gnd";
    d_ref.classname = "RefNode";
    d_ref.ports = {{"v", Port{}}};

    circuit.devices.push_back(d_bat);
    circuit.devices.push_back(d_load);
    circuit.devices.push_back(d_ref);
    circuit.connections = {
        {"bat.v_out", "load.v_in", {}},
        {"load.v_out", "gnd.v", {}},
        {"bat.v_in", "gnd.v", {}}
    };
    registry.register_type("test_circuit", circuit);

    auto result = CodeGen::generate_composite_systems(circuit, registry);

    // Verify .commit() calls are present in generated source
    EXPECT_NE(result.source.find(".commit(*st, dt)"), std::string::npos)
        << "Generated step_N() must call .commit() on devices for battery discharge to work";

    // Verify .execute() calls are also present
    EXPECT_NE(result.source.find(".execute(*st, dt)"), std::string::npos)
        << "Generated step_N() must call .execute() on devices";

    // Verify commit comes after execute in the generated code
    size_t exec_pos = result.source.find(".execute(*st, dt)");
    size_t commit_pos = result.source.find(".commit(*st, dt)");
    if (exec_pos != std::string::npos && commit_pos != std::string::npos) {
        EXPECT_LT(exec_pos, commit_pos)
            << "execute() must come before commit() in generated step";
    }
}

TEST(AotComposite, GeneratedStepMethodsUseSourceConsumerOrdering) {
    ComponentRegistry registry;

    PrimitiveSpec refnode_out;
    refnode_out.classname = "RefNode";
    refnode_out.ports["v"] = Port{bp2::Direction::Output, PortType::V, std::nullopt};
    refnode_out.domains = {Domain::Electrical};
    refnode_out.solver.execution = {.electrical_passive = true};
    refnode_out.solver.scheduler_source = true;
    refnode_out.params["value"] = ParamSpec{ParamSchemaType::Float, "0.0"};
    SolverRole role;
    role.kind = "FixedVoltageNode";
    role.port_map["node"] = "v";
    role.param_map["voltage"] = "value";
    role.value_map["bind_handle"] = 1.0f;
    refnode_out.solver.solver_role = role;
    refnode_out.solver.solver_owned_electrical = false;
    registry.register_type("RefNode", refnode_out);

    PrimitiveSpec consumer_type;
    consumer_type.classname = "Voltmeter";
    consumer_type.ports["v"] = Port{bp2::Direction::Input, PortType::V, std::nullopt};
    consumer_type.domains = {Domain::Electrical};
    consumer_type.solver.scheduler_source = false;
    consumer_type.solver.execution = {.electrical_observer = true};
    registry.register_type("Voltmeter", consumer_type);

    CompositeSpec td;
    td.classname = "sched_order_test";

    DeviceInstance meter;
    meter.name = "meter";
    meter.classname = "Voltmeter";

    DeviceInstance src;
    src.name = "src";
    src.classname = "RefNode";

    // Intentionally reversed declaration order: consumer first, source second.
    td.devices.push_back(meter);
    td.devices.push_back(src);
    td.connections = {{"src.v", "meter.v", {}}};
    registry.register_type(td.classname, td);

    auto result = CodeGen::generate_composite_systems(td, registry);

    const size_t src_exec = result.source.find("src.execute(*st, dt)");
    const size_t meter_exec = result.source.find("meter.execute(*st, dt)");
    const size_t src_commit = result.source.find("src.commit(*st, dt)");
    const size_t meter_commit = result.source.find("meter.commit(*st, dt)");

    ASSERT_NE(src_exec, std::string::npos);
    ASSERT_NE(meter_exec, std::string::npos);
    ASSERT_NE(src_commit, std::string::npos);
    ASSERT_NE(meter_commit, std::string::npos);

    EXPECT_LT(src_exec, meter_exec) << "source execute must run before consumer execute";
    EXPECT_LT(meter_exec, src_commit) << "consumer execute must run before source commit (JIT parity)";
    EXPECT_LT(src_commit, meter_commit) << "source commit must run before consumer commit";
}

// =============================================================================
// Regression: AOT codegen must unify structural bridge ext↔port
// signals via UnionFind, matching JIT solver behavior. Without this, composites
// with bridge nodes have broken signal routing in AOT mode because ext and port
// get allocated as separate signals instead of being unified.
// =============================================================================
TEST(AotComposite, BridgeNodeExtPortUnification) {
    ComponentRegistry registry;

    // IndicatorLight (simple pass-through component)
    PrimitiveSpec light = *as_primitive(*test_registry().get("IndicatorLight"));
    registry.register_type("IndicatorLight", light);

    // Composite: vin→lamp→vout
    CompositeSpec composite;
    composite.classname = "bridge_test";

    DeviceInstance d_lamp;
    d_lamp.name = "lamp";
    d_lamp.classname = "IndicatorLight";
    d_lamp.params["conductance"] = "0.002";  // Required param for ConductanceBranch solver role
    composite.devices.push_back(d_lamp);
    composite.bridge_ports = {
        make_bridge_port_def("vin", bp2::BridgeDirection::Input, PortType::V),
        make_bridge_port_def("vout", bp2::BridgeDirection::Output, PortType::V),
    };
    composite.connections = {
        {"vin.port", "lamp.v_in", {}},
        {"lamp.v_out", "vout.port", {}}
    };
    composite.ports["vin"]  = Port{bp2::Direction::Input, PortType::V, std::nullopt};
    composite.ports["vout"] = Port{bp2::Direction::Output, PortType::V, std::nullopt};
    registry.register_type("bridge_test", composite);

    // Generate AOT code
    auto aot_result = CodeGen::generate_composite_systems(composite, registry);
    ASSERT_FALSE(aot_result.header.empty());

    // Extract SIGNAL_COUNT
    std::regex signal_count_re(R"(SIGNAL_COUNT\s*=\s*(\d+))");
    std::smatch match;
    ASSERT_TRUE(std::regex_search(aot_result.header, match, signal_count_re))
        << "AOT header should contain SIGNAL_COUNT";
    uint32_t aot_signal_count = static_cast<uint32_t>(std::stoul(match[1].str()));

    // Also run through JIT path (Flattener)
    ui::StringInterner jit_interner2;
    bp2::BlueprintLibrary jit_library2;
    for (const auto& [name, spec] : registry.all_types()) {
        if (is_composite(spec)) {
            auto bp = bp2::blueprint_from_type_definition(spec, jit_interner2, registry);
            jit_library2.add(jit_interner2.intern(name), std::move(bp));
        }
    }
    auto jit_bp2 = bp2::blueprint_from_type_definition(ComponentSpec{composite}, jit_interner2, registry);
    bp2::PathArena jit_arena2(jit_interner2);
    bp2::Flattener jit_flattener2(jit_library2);
    auto jit_netlist2 = jit_flattener2.flatten(jit_bp2, jit_arena2);
    auto jit_input2 = bp2::elaboration::elaborate_for_jit(jit_netlist2, jit_arena2, jit_interner2, registry);
    BuildResult jit_result = build_systems_dev(jit_input2);

    // Key assertion: vin.ext and vin.port MUST share a signal in JIT
    auto jit_sig = [&](const std::string& port) -> uint32_t {
        auto it = jit_result.port_to_signal.find(jit_result.signal_key_interner.lookup(port));
        EXPECT_NE(it, jit_result.port_to_signal.end()) << port << " should exist in JIT map";
        return it != jit_result.port_to_signal.end() ? it->second : UINT32_MAX;
    };

    EXPECT_EQ(jit_sig("vin.ext"), jit_sig("vin.port"))
        << "JIT must unify input bridge ext↔port";
    EXPECT_EQ(jit_sig("vout.ext"), jit_sig("vout.port"))
        << "JIT must unify output bridge ext↔port";

    // AOT signal count must match JIT exactly, including trailing sentinel slot.
    // Without bridge unification, AOT would allocate 2 extra signals (one for each bridge).
    EXPECT_EQ(aot_signal_count, jit_result.signal_count)
        << "AOT and JIT signal counts must match exactly";
}

TEST(AotComposite, DynamicSourcePatchingGeneratedForElectricalWrappers) {
    ComponentRegistry registry;

    PrimitiveSpec cvs;
    cvs.classname = "ControlledVoltageSource";
    cvs.ports["cmd"] = Port{bp2::Direction::Input, PortType::Any, std::nullopt};
    cvs.ports["gain"] = Port{bp2::Direction::Input, PortType::Any, std::nullopt};
    cvs.ports["offset"] = Port{bp2::Direction::Input, PortType::Any, std::nullopt};
    cvs.ports["min_v"] = Port{bp2::Direction::Input, PortType::Any, std::nullopt};
    cvs.ports["max_v"] = Port{bp2::Direction::Input, PortType::Any, std::nullopt};
    cvs.ports["v_pos"] = Port{bp2::Direction::Output, PortType::V, std::nullopt};
    cvs.ports["v_neg"] = Port{bp2::Direction::Input, PortType::V, std::nullopt};
    cvs.domains = {Domain::Electrical};
    cvs.solver.execution = {.electrical_passive = true};
    registry.register_type("ControlledVoltageSource", cvs);

    PrimitiveSpec vc;
    vc.classname = "VariableConductance";
    vc.ports["cmd"] = Port{bp2::Direction::Input, PortType::Any, std::nullopt};
    vc.ports["g_min"] = Port{bp2::Direction::Input, PortType::Any, std::nullopt};
    vc.ports["g_max"] = Port{bp2::Direction::Input, PortType::Any, std::nullopt};
    vc.ports["v_in"] = Port{bp2::Direction::Input, PortType::V, std::nullopt};
    vc.ports["v_out"] = Port{bp2::Direction::Output, PortType::V, std::nullopt};
    vc.domains = {Domain::Electrical};
    vc.solver.execution = {.electrical_passive = true};
    registry.register_type("VariableConductance", vc);

    registry.register_type("RefNode", *as_primitive(*test_registry().get("RefNode")));
    registry.register_type("Value", *as_primitive(*test_registry().get("Value")));

    CompositeSpec circuit;
    circuit.classname = "dynamic_patch_test";

    DeviceInstance d_ref;
    d_ref.name = "gnd";
    d_ref.classname = "RefNode";

    DeviceInstance d_cvs;
    d_cvs.name = "src";
    d_cvs.classname = "ControlledVoltageSource";

    DeviceInstance d_vc;
    d_vc.name = "load";
    d_vc.classname = "VariableConductance";

    DeviceInstance d_cmd;
    d_cmd.name = "cmd";
    d_cmd.classname = "Value";
    d_cmd.params["value"] = "0.5";

    DeviceInstance d_gain;
    d_gain.name = "gain";
    d_gain.classname = "Value";
    d_gain.params["value"] = "1.0";

    DeviceInstance d_offset;
    d_offset.name = "offset";
    d_offset.classname = "Value";
    d_offset.params["value"] = "0.0";

    DeviceInstance d_min;
    d_min.name = "minv";
    d_min.classname = "Value";
    d_min.params["value"] = "0.0";

    DeviceInstance d_max;
    d_max.name = "maxv";
    d_max.classname = "Value";
    d_max.params["value"] = "28.5";

    DeviceInstance d_gmin;
    d_gmin.name = "gmin";
    d_gmin.classname = "Value";
    d_gmin.params["value"] = "0.1";

    DeviceInstance d_gmax;
    d_gmax.name = "gmax";
    d_gmax.classname = "Value";
    d_gmax.params["value"] = "1.0";

    circuit.devices.push_back(d_ref);
    circuit.devices.push_back(d_cvs);
    circuit.devices.push_back(d_vc);
    circuit.devices.push_back(d_cmd);
    circuit.devices.push_back(d_gain);
    circuit.devices.push_back(d_offset);
    circuit.devices.push_back(d_min);
    circuit.devices.push_back(d_max);
    circuit.devices.push_back(d_gmin);
    circuit.devices.push_back(d_gmax);
    circuit.connections = {
        {"src.v_pos", "load.v_in", {}},
        {"load.v_out", "gnd.v", {}},
        {"src.v_neg", "gnd.v", {}},
        {"cmd.o", "src.cmd", {}},
        {"gain.o", "src.gain", {}},
        {"offset.o", "src.offset", {}},
        {"minv.o", "src.min_v", {}},
        {"maxv.o", "src.max_v", {}},
        {"cmd.o", "load.cmd", {}},
        {"gmin.o", "load.g_min", {}},
        {"gmax.o", "load.g_max", {}},
    };
    registry.register_type(circuit.classname, circuit);

    auto result = CodeGen::generate_composite_systems(circuit, registry);

    EXPECT_NE(result.source.find("electrical_rt_.element_value_a"), std::string::npos)
        << "AOT source should patch dynamic electrical element values before solve";
    EXPECT_NE(result.source.find("src.electrical_handle.element_id"), std::string::npos)
        << "CVS element_id patching should be generated";
    EXPECT_NE(result.source.find("load.electrical_handle.element_id"), std::string::npos)
        << "VariableConductance element_id patching should be generated";
    EXPECT_NE(result.source.find("std::clamp(cmd * gain + offset, min_v, max_v)"), std::string::npos)
        << "CVS affine clamp patch expression should be generated";
    EXPECT_NE(result.source.find("g_min + (g_max - g_min) * t"), std::string::npos)
        << "VariableConductance lerp patch expression should be generated";
}
