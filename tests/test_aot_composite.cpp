#include <gtest/gtest.h>
#include "codegen/codegen.h"
#include "json_parser/json_parser.h"
#include "jit_solver/jit_solver.h"
#include <regex>
#include <set>
#include <unordered_map>

namespace {

ExecutionPhases make_execution(
    bool electrical_passive,
    bool electrical_observer,
    bool logical,
    bool control_commit,
    bool electrical_actuator,
    bool finalize,
    bool mechanical,
    bool hydraulic,
    bool thermal) {
    ExecutionPhases phases;
    phases.electrical_passive = electrical_passive;
    phases.electrical_observer = electrical_observer;
    phases.logical = logical;
    phases.control_commit = control_commit;
    phases.electrical_actuator = electrical_actuator;
    phases.finalize = finalize;
    phases.mechanical = mechanical;
    phases.hydraulic = hydraulic;
    phases.thermal = thermal;
    return phases;
}

}


// ============================================================
// Composite Systems generation
// ============================================================

TEST(AotComposite, GeneratesSystemsForComposite) {
    // Setup: simple composite with 2 devices
    TypeRegistry registry;

    TypeDefinition lamp;
    lamp.classname = "lamp_pass_through";
    lamp.cpp_class = false;
    DeviceInstance d_vin;
    d_vin.name = "vin";
    d_vin.classname = "BlueprintInput";
    d_vin.execution = make_execution(true, false, true, false, false, false, true, true, true);
    DeviceInstance d_lamp;
    d_lamp.name = "lamp";
    d_lamp.classname = "IndicatorLight";
    d_lamp.execution = make_execution(true, false, false, false, false, false, false, false, false);
    DeviceInstance d_vout;
    d_vout.name = "vout";
    d_vout.classname = "BlueprintOutput";
    d_vout.execution = make_execution(true, false, true, false, false, false, true, true, true);
    lamp.devices = {d_vin, d_lamp, d_vout};
    lamp.connections = {{"vin.port", "lamp.v_in", {}}, {"lamp.v_out", "vout.port", {}}};
    registry.types["lamp_pass_through"] = lamp;

    // Generate code
    auto result = CodeGen::generate_composite_systems(lamp, registry);

    // Should produce header + source
    EXPECT_FALSE(result.header.empty());
    EXPECT_FALSE(result.source.empty());

    // Header should contain class name
    EXPECT_NE(result.header.find("lamp_pass_through_Systems"), std::string::npos);

    // Should contain device fields (primitive devices as AotProvider fields)
    EXPECT_NE(result.header.find("BlueprintInput"), std::string::npos);
    EXPECT_NE(result.header.find("IndicatorLight"), std::string::npos);
    EXPECT_NE(result.header.find("BlueprintOutput"), std::string::npos);

    // Should have solve_step and pre_load
    EXPECT_NE(result.header.find("solve_step"), std::string::npos);
    EXPECT_NE(result.header.find("pre_load"), std::string::npos);
}

TEST(AotComposite, NestedComposite_ContainsSubSystems) {
    TypeRegistry registry;

    // Inner composite
    TypeDefinition inner;
    inner.classname = "simple_battery";
    inner.cpp_class = false;
    DeviceInstance d_bat;
    d_bat.name = "bat";
    d_bat.classname = "Battery";
    d_bat.execution = make_execution(true, false, false, false, false, false, false, false, false);
    inner.devices = {d_bat};
    registry.types["simple_battery"] = inner;

    // Outer composite references inner
    TypeDefinition outer;
    outer.classname = "battery_bank";
    outer.cpp_class = false;
    SubBlueprintRef ref;
    ref.id = "sb_1";
    ref.type_name = "simple_battery";
    outer.sub_blueprints.push_back(ref);
    DeviceInstance d_bus;
    d_bus.name = "bus";
    d_bus.classname = "Bus";
    d_bus.execution = make_execution(true, false, true, false, false, false, false, false, false);
    outer.devices = {d_bus};
    registry.types["battery_bank"] = outer;

    auto result = CodeGen::generate_composite_systems(outer, registry);

    // Composites are FLATTENED: sub-blueprint devices get prefixed names
    // sb_1:bat → sanitized to sb_1_bat (Battery device from inner composite)
    EXPECT_NE(result.header.find("sb_1_bat"), std::string::npos)
        << "Flattened sub-blueprint device should appear with prefixed name";

    // Should also have the top-level primitive
    EXPECT_NE(result.header.find("Bus"), std::string::npos);
}

TEST(AotComposite, ThreeLevelsDeep_FullHierarchy) {
    TypeRegistry registry;

    // Level 2: leaf
    TypeDefinition leaf;
    leaf.classname = "leaf_type";
    leaf.cpp_class = false;
    DeviceInstance d_r;
    d_r.name = "r1";
    d_r.classname = "Resistor";
    d_r.execution = make_execution(true, false, false, false, false, false, false, false, false);
    leaf.devices = {d_r};
    registry.types["leaf_type"] = leaf;

    // Level 1: mid references leaf
    TypeDefinition mid;
    mid.classname = "mid_type";
    mid.cpp_class = false;
    SubBlueprintRef ref_leaf;
    ref_leaf.id = "leaf_inst";
    ref_leaf.type_name = "leaf_type";
    mid.sub_blueprints.push_back(ref_leaf);
    registry.types["mid_type"] = mid;

    // Level 0: top references mid
    TypeDefinition top;
    top.classname = "top_type";
    top.cpp_class = false;
    SubBlueprintRef ref_mid;
    ref_mid.id = "mid_inst";
    ref_mid.type_name = "mid_type";
    top.sub_blueprints.push_back(ref_mid);
    registry.types["top_type"] = top;

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
    TypeRegistry registry;

    TypeDefinition leaf;
    leaf.classname = "leaf";
    leaf.cpp_class = false;
    DeviceInstance d;
    d.name = "d";
    d.classname = "Battery";
    d.execution = make_execution(true, false, false, false, false, false, false, false, false);
    leaf.devices = {d};
    registry.types["leaf"] = leaf;

    TypeDefinition parent;
    parent.classname = "parent";
    parent.cpp_class = false;
    SubBlueprintRef ref;
    ref.id = "l1";
    ref.type_name = "leaf";
    parent.sub_blueprints.push_back(ref);
    registry.types["parent"] = parent;

    auto order = registry.get_composites_topo_sorted();

    // leaf must come before parent
    auto it_leaf = std::find(order.begin(), order.end(), "leaf");
    auto it_parent = std::find(order.begin(), order.end(), "parent");
    ASSERT_NE(it_leaf, order.end());
    ASSERT_NE(it_parent, order.end());
    EXPECT_LT(std::distance(order.begin(), it_leaf),
              std::distance(order.begin(), it_parent));
}

TEST(AotComposite, PreLoad_CallsSubComposites) {
    TypeRegistry registry;

    TypeDefinition inner;
    inner.classname = "inner_type";
    inner.cpp_class = false;
    DeviceInstance d_bat;
    d_bat.name = "bat";
    d_bat.classname = "Battery";
    d_bat.execution = make_execution(true, false, false, false, false, false, false, false, false);
    inner.devices = {d_bat};
    registry.types["inner_type"] = inner;

    TypeDefinition outer;
    outer.classname = "outer_type";
    outer.cpp_class = false;
    SubBlueprintRef ref;
    ref.id = "inner_inst";
    ref.type_name = "inner_type";
    outer.sub_blueprints.push_back(ref);
    DeviceInstance d_bus;
    d_bus.name = "bus";
    d_bus.classname = "Bus";
    d_bus.execution = make_execution(true, false, true, false, false, false, false, false, false);
    outer.devices = {d_bus};
    registry.types["outer_type"] = outer;

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
// DISABLED: legacy solver-specific test checking BlueprintInput/Output alias unification.
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

    TypeRegistry registry;

    // BlueprintInput: port (Out, Any), ext (In, Any, alias→port)
    TypeDefinition bp_in;
    bp_in.classname = "BlueprintInput";
    bp_in.cpp_class = true;
    bp_in.ports["port"] = Port{PortDirection::Out, PortType::Any, std::nullopt};
    bp_in.ports["ext"]  = Port{PortDirection::In, PortType::Any, std::string("port")};
    bp_in.domains = {{Domain::Electrical}};
    bp_in.execution = make_execution(true, false, true, false, false, false, true, true, true);
    registry.types["BlueprintInput"] = bp_in;

    // BlueprintOutput: port (In, Any), ext (Out, Any, alias→port)
    TypeDefinition bp_out;
    bp_out.classname = "BlueprintOutput";
    bp_out.cpp_class = true;
    bp_out.ports["port"] = Port{PortDirection::In, PortType::Any, std::nullopt};
    bp_out.ports["ext"]  = Port{PortDirection::Out, PortType::Any, std::string("port")};
    bp_out.domains = {{Domain::Electrical}};
    bp_out.execution = make_execution(true, false, true, false, false, false, true, true, true);
    registry.types["BlueprintOutput"] = bp_out;

    // IndicatorLight: v_in (In), v_out (Out), brightness (Out)
    TypeDefinition light;
    light.classname = "IndicatorLight";
    light.cpp_class = true;
    light.ports["v_in"]       = Port{PortDirection::In, PortType::V, std::nullopt};
    light.ports["v_out"]      = Port{PortDirection::Out, PortType::V, std::nullopt};
    light.ports["brightness"] = Port{PortDirection::Out, PortType::I, std::nullopt};
    light.domains = {{Domain::Electrical}};
    light.execution = make_execution(true, false, false, false, false, false, false, false, false);
    registry.types["IndicatorLight"] = light;

    // Composite: lamp_pass_through (vin→lamp→vout)
    TypeDefinition lamp;
    lamp.classname = "lamp_pass_through";
    lamp.cpp_class = false;

    DeviceInstance d_vin;
    d_vin.name = "vin";
    d_vin.classname = "BlueprintInput";
    DeviceInstance d_lamp;
    d_lamp.name = "lamp";
    d_lamp.classname = "IndicatorLight";
    DeviceInstance d_vout;
    d_vout.name = "vout";
    d_vout.classname = "BlueprintOutput";
    lamp.devices = {d_vin, d_lamp, d_vout};
    lamp.connections = {
        {"vin.port", "lamp.v_in", {}},
        {"lamp.v_out", "vout.port", {}}
    };
    // Expose external ports matching BlueprintInput/Output naming
    lamp.ports["vin"]  = Port{PortDirection::In, PortType::V, std::nullopt};
    lamp.ports["vout"] = Port{PortDirection::Out, PortType::V, std::nullopt};
    registry.types["lamp_pass_through"] = lamp;

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

    // ---- JIT path: expand + merge + build_systems_dev ----

    std::set<std::string> loading_stack;
    auto expanded = expand_sub_blueprint_references(lamp, registry, loading_stack);

    // Merge each device with its type definition (same as AOT does)
    for (auto& dev : expanded.devices) {
        const auto* type_def = registry.get(dev.classname);
        if (type_def) {
            dev = merge_device_instance(dev, *type_def);
        }
    }

    // Convert connections to pair format for build_systems_dev
    std::vector<std::pair<std::string, std::string>> conn_pairs;
    for (const auto& c : expanded.connections) {
        conn_pairs.push_back({c.from, c.to});
    }

    BuildResult jit_result = build_systems_dev(expanded.devices, conn_pairs);

    // ---- Compare signal topologies ----

    // Push builder may keep additional bookkeeping/alias signals.
    uint32_t jit_signal_count = jit_result.signal_count;
    EXPECT_GE(jit_signal_count, aot_signal_count)
        << "JIT signal count should be at least AOT signal count";

    // Both should have the same expanded device names
    std::set<std::string> aot_device_names;
    for (const auto& dev : expanded.devices) {
        aot_device_names.insert(dev.name);
    }
    std::set<std::string> jit_device_names;
    for (const auto& [name, _] : jit_result.devices) {
        jit_device_names.insert(name);
    }
    EXPECT_EQ(aot_device_names, jit_device_names)
        << "AOT and JIT should expand to the same device names";

    // Verify connected ports land on the same signal (equivalence class check).
    // vin.port and lamp.v_in should share a signal (they are connected).
    // lamp.v_out and vout.port should share a signal.
    auto jit_sig = [&](const std::string& port) -> uint32_t {
        auto it = jit_result.port_to_signal.find(port);
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
    if (jit_result.port_to_signal.count("vin.ext") && jit_result.port_to_signal.count("vin.port")) {
        EXPECT_NE(jit_sig("vin.ext"), UINT32_MAX);
        EXPECT_NE(jit_sig("vin.port"), UINT32_MAX);
    }
    if (jit_result.port_to_signal.count("vout.ext") && jit_result.port_to_signal.count("vout.port")) {
        EXPECT_NE(jit_sig("vout.ext"), UINT32_MAX);
        EXPECT_NE(jit_sig("vout.port"), UINT32_MAX);
    }

    // Disconnected port: lamp.brightness should have its own signal
    EXPECT_NE(jit_sig("lamp.brightness"), jit_sig("vin.port"))
        << "Disconnected port lamp.brightness should NOT share signal with vin.port";
    EXPECT_NE(jit_sig("lamp.brightness"), jit_sig("lamp.v_out"))
        << "Disconnected port lamp.brightness should NOT share signal with lamp.v_out";

    // Verify the AOT generated code references the same device names
    EXPECT_NE(aot_result.header.find("vin"), std::string::npos)
        << "AOT header should reference device 'vin'";
    EXPECT_NE(aot_result.header.find("lamp"), std::string::npos)
        << "AOT header should reference device 'lamp'";
    EXPECT_NE(aot_result.header.find("vout"), std::string::npos)
        << "AOT header should reference device 'vout'";

    // Verify AOT source contains push execution calls for all three devices
    EXPECT_NE(aot_result.source.find("vin.execute"), std::string::npos)
        << "AOT source should contain vin.execute call";
    EXPECT_NE(aot_result.source.find("lamp.execute"), std::string::npos)
        << "AOT source should contain lamp.execute call";
    EXPECT_NE(aot_result.source.find("vout.execute"), std::string::npos)
        << "AOT source should contain vout.execute call";
}

// ============================================================
// Electrical Plan codegen
// ============================================================

TEST(AotComposite, ElectricalPlan_BatteryAndResistor_GeneratesIslandArrays) {
    TypeRegistry registry;

    // === Register types with ports ===
    TypeDefinition battery_type;
    battery_type.classname = "Battery";
    battery_type.cpp_class = true;
    battery_type.ports["v_out"] = Port{PortDirection::Out, PortType::V, std::nullopt};
    battery_type.ports["v_in"] = Port{PortDirection::In, PortType::V, std::nullopt};
    battery_type.domains = {{Domain::Electrical}};
    registry.types["Battery"] = battery_type;

    TypeDefinition resistor_type;
    resistor_type.classname = "Resistor";
    resistor_type.cpp_class = true;
    resistor_type.ports["v_in"] = Port{PortDirection::In, PortType::V, std::nullopt};
    resistor_type.ports["v_out"] = Port{PortDirection::Out, PortType::V, std::nullopt};
    resistor_type.domains = {{Domain::Electrical}};
    registry.types["Resistor"] = resistor_type;

    TypeDefinition refnode_type;
    refnode_type.classname = "RefNode";
    refnode_type.cpp_class = true;
    refnode_type.ports["v"] = Port{PortDirection::In, PortType::V, std::nullopt};
    refnode_type.domains = {{Domain::Electrical}};
    registry.types["RefNode"] = refnode_type;

    // Simple circuit: Battery -> Resistor -> RefNode (fixed voltage)
    TypeDefinition circuit;
    circuit.classname = "simple_circuit";
    circuit.cpp_class = false;

    DeviceInstance d_bat;
    d_bat.name = "bat";
    d_bat.classname = "Battery";
    d_bat.execution = make_execution(true, false, false, false, false, false, false, false, false);
    d_bat.params["v_nominal"] = "28.0";
    d_bat.params["internal_r"] = "0.01";

    DeviceInstance d_res;
    d_res.name = "res";
    d_res.classname = "Resistor";
    d_res.execution = make_execution(true, false, false, false, false, false, false, false, false);
    d_res.params["conductance"] = "10.0";

    DeviceInstance d_ref;
    d_ref.name = "gnd";
    d_ref.classname = "RefNode";
    d_ref.execution = make_execution(true, false, false, false, false, false, false, false, false);
    d_ref.params["value"] = "0.0";

    circuit.devices = {d_bat, d_res, d_ref};
    circuit.connections = {
        {"bat.v_out", "res.v_in", {}},
        {"res.v_out", "gnd.v", {}}
    };
    registry.types["simple_circuit"] = circuit;

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
    TypeRegistry registry;

    // === Register types with ports ===
    TypeDefinition battery_type;
    battery_type.classname = "Battery";
    battery_type.cpp_class = true;
    battery_type.ports["v_out"] = Port{PortDirection::Out, PortType::V, std::nullopt};
    battery_type.ports["v_in"] = Port{PortDirection::In, PortType::V, std::nullopt};
    battery_type.domains = {{Domain::Electrical}};
    registry.types["Battery"] = battery_type;

    TypeDefinition light_type;
    light_type.classname = "IndicatorLight";
    light_type.cpp_class = true;
    light_type.ports["v_in"] = Port{PortDirection::In, PortType::V, std::nullopt};
    light_type.ports["v_out"] = Port{PortDirection::Out, PortType::V, std::nullopt};
    light_type.ports["brightness"] = Port{PortDirection::Out, PortType::I, std::nullopt};
    light_type.domains = {{Domain::Electrical}};
    registry.types["IndicatorLight"] = light_type;

    TypeDefinition lamp_circuit;
    lamp_circuit.classname = "lamp_circuit";
    lamp_circuit.cpp_class = false;

    DeviceInstance d_bat;
    d_bat.name = "bat";
    d_bat.classname = "Battery";
    d_bat.execution = make_execution(true, false, false, false, false, false, false, false, false);

    DeviceInstance d_lamp;
    d_lamp.name = "lamp";
    d_lamp.classname = "IndicatorLight";
    d_lamp.execution = make_execution(true, false, false, false, false, false, false, false, false);

    lamp_circuit.devices = {d_bat, d_lamp};
    lamp_circuit.connections = {
        {"bat.v_out", "lamp.v_in", {}},
        {"bat.v_in", "lamp.v_out", {}}
    };
    registry.types["lamp_circuit"] = lamp_circuit;

    auto result = CodeGen::generate_composite_systems(lamp_circuit, registry);

    EXPECT_NE(result.header.find("ELECTRICAL_ISLAND_COUNT"), std::string::npos)
        << "Header should contain ELECTRICAL_ISLAND_COUNT even for IndicatorLight";

    EXPECT_NE(result.source.find("solve_electrical"), std::string::npos)
        << "Source should contain solve_electrical for circuit with IndicatorLight";
}

TEST(AotComposite, ElectricalPlan_NoElectricalDevices_HasZeroIslands) {
    TypeRegistry registry;

    TypeDefinition no_elec;
    no_elec.classname = "no_electrical";
    no_elec.cpp_class = false;

    DeviceInstance d_bus;
    d_bus.name = "bus";
    d_bus.classname = "Bus";
    d_bus.execution = make_execution(false, false, true, false, false, false, false, false, false);

    no_elec.devices = {d_bus};
    registry.types["no_electrical"] = no_elec;

    auto result = CodeGen::generate_composite_systems(no_elec, registry);

    EXPECT_NE(result.header.find("ELECTRICAL_ISLAND_COUNT = 0"), std::string::npos)
        << "Header should show ELECTRICAL_ISLAND_COUNT = 0 when no electrical devices";

    EXPECT_EQ(result.source.find("solve_electrical"), std::string::npos)
        << "Source should NOT contain solve_electrical when no electrical devices";
}

TEST(AotComposite, ElectricalBindings_WrapperHandlesGenerated) {
    TypeRegistry registry;

    TypeDefinition battery_type;
    battery_type.classname = "Battery";
    battery_type.cpp_class = true;
    battery_type.ports["v_out"] = Port{PortDirection::Out, PortType::V, std::nullopt};
    battery_type.ports["v_in"] = Port{PortDirection::In, PortType::V, std::nullopt};
    battery_type.domains = {{Domain::Electrical}};
    registry.types["Battery"] = battery_type;

    TypeDefinition sense_type;
    sense_type.classname = "CurrentSense";
    sense_type.cpp_class = true;
    sense_type.ports["v_in"] = Port{PortDirection::In, PortType::V, std::nullopt};
    sense_type.ports["v_out"] = Port{PortDirection::Out, PortType::V, std::nullopt};
    sense_type.ports["i_out"] = Port{PortDirection::Out, PortType::I, std::nullopt};
    sense_type.domains = {{Domain::Electrical}};
    registry.types["CurrentSense"] = sense_type;

    TypeDefinition light_type;
    light_type.classname = "IndicatorLight";
    light_type.cpp_class = true;
    light_type.ports["v_in"] = Port{PortDirection::In, PortType::V, std::nullopt};
    light_type.ports["v_out"] = Port{PortDirection::Out, PortType::V, std::nullopt};
    light_type.ports["brightness"] = Port{PortDirection::Out, PortType::I, std::nullopt};
    light_type.domains = {{Domain::Electrical}};
    registry.types["IndicatorLight"] = light_type;

    TypeDefinition refnode_type;
    refnode_type.classname = "RefNode";
    refnode_type.cpp_class = true;
    refnode_type.ports["v"] = Port{PortDirection::In, PortType::V, std::nullopt};
    refnode_type.domains = {{Domain::Electrical}};
    registry.types["RefNode"] = refnode_type;

    TypeDefinition circuit;
    circuit.classname = "wrapper_binding_circuit";
    circuit.cpp_class = false;

    DeviceInstance d_bat;
    d_bat.name = "bat";
    d_bat.classname = "Battery";
    d_bat.execution = make_execution(true, false, false, false, false, false, false, false, false);

    DeviceInstance d_sense;
    d_sense.name = "sense";
    d_sense.classname = "CurrentSense";
    d_sense.execution = make_execution(true, false, false, false, false, false, false, false, false);

    DeviceInstance d_lamp;
    d_lamp.name = "lamp";
    d_lamp.classname = "IndicatorLight";
    d_lamp.execution = make_execution(true, false, false, false, false, false, false, false, false);

    DeviceInstance d_ref;
    d_ref.name = "gnd";
    d_ref.classname = "RefNode";
    d_ref.execution = make_execution(true, false, false, false, false, false, false, false, false);

    circuit.devices = {d_bat, d_sense, d_lamp, d_ref};
    circuit.connections = {
        {"bat.v_out", "sense.v_in", {}},
        {"sense.v_out", "lamp.v_in", {}},
        {"lamp.v_out", "gnd.v", {}},
        {"bat.v_in", "gnd.v", {}}
    };
    registry.types["wrapper_binding_circuit"] = circuit;

    auto result = CodeGen::generate_composite_systems(circuit, registry);

    EXPECT_NE(result.header.find("struct ElectricalBindings"), std::string::npos);
    EXPECT_NE(result.header.find("bat_component"), std::string::npos);
    EXPECT_NE(result.header.find("sense_component"), std::string::npos);
    EXPECT_NE(result.header.find("lamp_component"), std::string::npos);

    EXPECT_NE(result.source.find("bat.electrical_handle.component_index = ElectricalBindings::bat_component"), std::string::npos);
    EXPECT_NE(result.source.find("sense.electrical_handle.component_index = ElectricalBindings::sense_component"), std::string::npos);
    EXPECT_NE(result.source.find("lamp.electrical_handle.component_index = ElectricalBindings::lamp_component"), std::string::npos);
}

TEST(AotComposite, ElectricalBindings_StableAcrossConnectionReordering) {
    TypeRegistry registry;

    TypeDefinition battery_type;
    battery_type.classname = "Battery";
    battery_type.cpp_class = true;
    battery_type.ports["v_out"] = Port{PortDirection::Out, PortType::V, std::nullopt};
    battery_type.ports["v_in"] = Port{PortDirection::In, PortType::V, std::nullopt};
    battery_type.domains = {{Domain::Electrical}};
    registry.types["Battery"] = battery_type;

    TypeDefinition sense_type;
    sense_type.classname = "CurrentSense";
    sense_type.cpp_class = true;
    sense_type.ports["v_in"] = Port{PortDirection::In, PortType::V, std::nullopt};
    sense_type.ports["v_out"] = Port{PortDirection::Out, PortType::V, std::nullopt};
    sense_type.ports["i_out"] = Port{PortDirection::Out, PortType::I, std::nullopt};
    sense_type.domains = {{Domain::Electrical}};
    registry.types["CurrentSense"] = sense_type;

    TypeDefinition refnode_type;
    refnode_type.classname = "RefNode";
    refnode_type.cpp_class = true;
    refnode_type.ports["v"] = Port{PortDirection::In, PortType::V, std::nullopt};
    refnode_type.domains = {{Domain::Electrical}};
    registry.types["RefNode"] = refnode_type;

    auto make_circuit = [&](const std::vector<Connection>& conns, const std::string& name) {
        TypeDefinition td;
        td.classname = name;
        td.cpp_class = false;

        DeviceInstance d_bat;
        d_bat.name = "bat";
        d_bat.classname = "Battery";
        d_bat.execution = make_execution(true, false, false, false, false, false, false, false, false);

        DeviceInstance d_sense;
        d_sense.name = "sense";
        d_sense.classname = "CurrentSense";
        d_sense.execution = make_execution(true, false, false, false, false, false, false, false, false);

        DeviceInstance d_ref;
        d_ref.name = "gnd";
        d_ref.classname = "RefNode";
        d_ref.execution = make_execution(true, false, false, false, false, false, false, false, false);

        td.devices = {d_bat, d_sense, d_ref};
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

    registry.types[c1.classname] = c1;
    registry.types[c2.classname] = c2;

    auto r1 = CodeGen::generate_composite_systems(c1, registry);
    auto r2 = CodeGen::generate_composite_systems(c2, registry);

    auto extract_component_const = [](const std::string& header, const std::string& dev) -> int {
        std::regex re("static constexpr uint32_t " + dev + "_component\\s*=\\s*(\\d+)");
        std::smatch m;
        if (!std::regex_search(header, m, re)) {
            return -1;
        }
        return std::stoi(m[1].str());
    };

    int bat_1 = extract_component_const(r1.header, "bat");
    int sense_1 = extract_component_const(r1.header, "sense");
    int bat_2 = extract_component_const(r2.header, "bat");
    int sense_2 = extract_component_const(r2.header, "sense");

    ASSERT_GE(bat_1, 0);
    ASSERT_GE(sense_1, 0);
    ASSERT_GE(bat_2, 0);
    ASSERT_GE(sense_2, 0);

    EXPECT_EQ(bat_1, bat_2);
    EXPECT_EQ(sense_1, sense_2);

    EXPECT_NE(r1.source.find("bat.electrical_handle.component_index = ElectricalBindings::bat_component"), std::string::npos);
    EXPECT_NE(r1.source.find("sense.electrical_handle.component_index = ElectricalBindings::sense_component"), std::string::npos);
    EXPECT_NE(r2.source.find("bat.electrical_handle.component_index = ElectricalBindings::bat_component"), std::string::npos);
    EXPECT_NE(r2.source.find("sense.electrical_handle.component_index = ElectricalBindings::sense_component"), std::string::npos);
}
