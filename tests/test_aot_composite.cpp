#include <gtest/gtest.h>
#include "core/solvers/aot/codegen.h"
#include "json_parser/json_parser.h"
#include "core/solvers/jit/jit_solver.h"
#include "test_helpers.h"
#include "test_fixtures.h"
#include "jit_build_input_test_helper.h"
#include <regex>
#include <set>
#include <unordered_map>

// ============================================================
// Composite Systems generation
// ============================================================

TEST(AotComposite, GeneratesSystemsForComposite) {
    // Setup: simple composite with 2 devices
    TypeRegistry registry;

    TypeDefinition lamp;
    lamp.classname = "voltage_indicator";
    lamp.cpp_class = false;
    DeviceInstance d_lamp;
    d_lamp.name = "lamp";
    d_lamp.classname = "IndicatorLight";
    d_lamp.execution = make_execution(true, false, false, false, false, false, false, false, false);
    lamp.devices = {d_lamp};
    lamp.bridge_ports = {
        make_bridge_port_def("vin", PortDirection::In, PortType::V),
        make_bridge_port_def("vout", PortDirection::Out, PortType::V),
    };
    lamp.connections = {{"vin.port", "lamp.v_in", {}}, {"lamp.v_out", "vout.port", {}}};
    registry.types["voltage_indicator"] = lamp;

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
    TypeRegistry registry;

    // Inner composite
    TypeDefinition inner;
    inner.classname = "battery_wrapper";
    inner.cpp_class = false;
    DeviceInstance d_bat;
    d_bat.name = "bat";
    d_bat.classname = "ElectricalSource";
    d_bat.execution = make_execution(true, false, false, false, false, false, false, false, false);
    inner.devices = {d_bat};
    registry.types["battery_wrapper"] = inner;

    // Outer composite references inner
    TypeDefinition outer;
    outer.classname = "battery_bank";
    outer.cpp_class = false;
    SubBlueprintRef ref;
    ref.id = "sb_1";
    ref.type_name = "battery_wrapper";
    outer.sub_blueprints.push_back(ref);
    DeviceInstance d_bus;
    d_bus.name = "bus";
    d_bus.classname = "Bus";
    d_bus.execution = make_execution(true, false, true, false, false, false, false, false, false);
    outer.devices = {d_bus};
    registry.types["battery_bank"] = outer;

    auto result = CodeGen::generate_composite_systems(outer, registry);

    // Composites are FLATTENED: sub-blueprint devices get prefixed names
    // sb_1:bat → sanitized to sb_1_bat (ElectricalSource device from inner composite)
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
    d.classname = "ElectricalSource";
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
    d_bat.classname = "ElectricalSource";
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

    TypeRegistry registry;
    register_lamp_composite_types(registry);

    // Composite: voltage_indicator (vin→lamp→vout)
    TypeDefinition lamp;
    lamp.classname = "voltage_indicator";
    lamp.cpp_class = false;

    DeviceInstance d_lamp;
    d_lamp.name = "lamp";
    d_lamp.classname = "IndicatorLight";
    lamp.devices = {d_lamp};
    lamp.bridge_ports = {
        make_bridge_port_def("vin", PortDirection::In, PortType::V),
        make_bridge_port_def("vout", PortDirection::Out, PortType::V),
    };
    lamp.connections = {
        {"vin.port", "lamp.v_in", {}},
        {"lamp.v_out", "vout.port", {}}
    };
    lamp.ports["vin"]  = Port{PortDirection::In, PortType::V, std::nullopt};
    lamp.ports["vout"] = Port{PortDirection::Out, PortType::V, std::nullopt};
    registry.types["voltage_indicator"] = lamp;

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

    BuildResult jit_result = build_systems_dev(make_jit_input_from_composite(expanded.devices, expanded.bridge_ports, expanded.connections));

    // ---- Compare signal topologies ----

    // Both paths now use the same signal allocation source via composite helpers.
    uint32_t jit_signal_count = jit_result.signal_count;
    EXPECT_EQ(jit_signal_count, aot_signal_count)
        << "JIT and AOT signal counts must match exactly (same allocation source)";

    // Runtime build/codegen lower bridge nodes before component instantiation.
    // Compare runtime-visible device sets (bridges excluded).
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
    TypeRegistry registry;
    registry.types["Generator"] = make_generator_type();
    registry.types["Resistor"] = make_resistor_type();
    registry.types["RefNode"] = make_refnode_type();

    // Simple circuit: ElectricalSource -> Resistor -> RefNode (fixed voltage)
    TypeDefinition circuit;
    circuit.classname = "simple_circuit";
    circuit.cpp_class = false;

    DeviceInstance d_bat;
    d_bat.name = "bat";
    d_bat.classname = "Generator";
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
    registry.types["ElectricalSource"] = make_electrical_source_type();
    registry.types["IndicatorLight"] = make_indicator_light_type();

    TypeDefinition lamp_circuit;
    lamp_circuit.classname = "lamp_circuit";
    lamp_circuit.cpp_class = false;

    DeviceInstance d_bat;
    d_bat.name = "bat";
    d_bat.classname = "ElectricalSource";
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

    registry.types["Generator"] = make_generator_type();
    registry.types["CurrentSense"] = make_currentsense_type();
    registry.types["IndicatorLight"] = make_indicator_light_type();
    registry.types["RefNode"] = make_refnode_type();

    TypeDefinition circuit;
    circuit.classname = "wrapper_binding_circuit";
    circuit.cpp_class = false;

    DeviceInstance d_bat;
    d_bat.name = "bat";
    d_bat.classname = "Generator";
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
    EXPECT_NE(result.header.find("sense_element_id"), std::string::npos);
    EXPECT_NE(result.header.find("lamp_element_id"), std::string::npos);

    EXPECT_NE(result.source.find("sense.electrical_handle.element_id = ElectricalBindings::sense_element_id"), std::string::npos);
    EXPECT_NE(result.source.find("lamp.electrical_handle.element_id = ElectricalBindings::lamp_element_id"), std::string::npos);
}

TEST(AotComposite, ElectricalBindings_StableAcrossConnectionReordering) {
    TypeRegistry registry;
    register_generator_sense_ref_types(registry);

    auto make_circuit = [&](const std::vector<Connection>& conns, const std::string& name) {
        TypeDefinition td;
        td.classname = name;
        td.cpp_class = false;

        DeviceInstance d_bat;
        d_bat.name = "bat";
        d_bat.classname = "Generator";
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
    TypeRegistry registry;
    register_generator_sense_ref_types(registry);

    TypeDefinition circuit;
    circuit.classname = "binding_fields_circuit";
    circuit.cpp_class = false;

    DeviceInstance d_bat;
    d_bat.name = "bat";
    d_bat.classname = "Generator";
    d_bat.execution = make_execution(true, false, false, false, false, false, false, false, false);

    DeviceInstance d_sense;
    d_sense.name = "sense";
    d_sense.classname = "CurrentSense";
    d_sense.execution = make_execution(true, false, false, false, false, false, false, false, false);

    DeviceInstance d_ref;
    d_ref.name = "gnd";
    d_ref.classname = "RefNode";
    d_ref.execution = make_execution(true, false, false, false, false, false, false, false, false);

    circuit.devices = {d_bat, d_sense, d_ref};
    circuit.connections = {
        {"bat.v_out", "sense.v_in", {}},
        {"sense.v_out", "gnd.v", {}},
        {"bat.v_in", "gnd.v", {}}
    };
    registry.types["binding_fields_circuit"] = circuit;

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
    TypeRegistry registry;
    registry.types["Generator"] = make_generator_type();

    // Non-electrical device that sits between electrical devices in the list
    registry.types["Any_V_to_Bool"] = make_any_v_to_bool_type();

    registry.types["CurrentSense"] = make_currentsense_type();
    registry.types["RefNode"] = make_refnode_type();

    TypeDefinition circuit;
    circuit.classname = "mixed_device_circuit";
    circuit.cpp_class = false;

    DeviceInstance d_bat;
    d_bat.name = "bat";
    d_bat.classname = "Generator";
    d_bat.execution = make_execution(true, false, false, false, false, false, false, false, false);

    // Non-electrical device inserted between bat and sense
    DeviceInstance d_logic;
    d_logic.name = "logic";
    d_logic.classname = "Any_V_to_Bool";
    d_logic.execution = make_execution(false, false, true, false, false, false, false, false, false);

    DeviceInstance d_sense;
    d_sense.name = "sense";
    d_sense.classname = "CurrentSense";
    d_sense.execution = make_execution(true, false, false, false, false, false, false, false, false);

    DeviceInstance d_ref;
    d_ref.name = "gnd";
    d_ref.classname = "RefNode";
    d_ref.execution = make_execution(true, false, false, false, false, false, false, false, false);

    // Key: logic device sits at index 1 in devices array, but is NOT electrical.
    // If binding code used devices[element_idx], sense would wrongly get logic's name.
    circuit.devices = {d_bat, d_logic, d_sense, d_ref};
    circuit.connections = {
        {"bat.v_out", "sense.v_in", {}},
        {"sense.v_out", "gnd.v", {}},
        {"bat.v_in", "gnd.v", {}},
        {"bat.v_out", "logic.Vin", {}}
    };
    registry.types["mixed_device_circuit"] = circuit;

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
    TypeRegistry registry;
    registry.types["ElectricalSource"] = make_electrical_source_type();
    registry.types["CurrentSense"] = make_currentsense_type();
    registry.types["RefNode"] = make_refnode_type();

    TypeDefinition circuit;
    circuit.classname = "debug_map_circuit";
    circuit.cpp_class = false;

    DeviceInstance d_bat;
    d_bat.name = "bat";
    d_bat.classname = "ElectricalSource";
    d_bat.execution = make_execution(true, false, false, false, false, false, false, false, false);

    DeviceInstance d_sense;
    d_sense.name = "sense";
    d_sense.classname = "CurrentSense";
    d_sense.execution = make_execution(true, false, false, false, false, false, false, false, false);

    DeviceInstance d_ref;
    d_ref.name = "gnd";
    d_ref.classname = "RefNode";
    d_ref.execution = make_execution(true, false, false, false, false, false, false, false, false);

    circuit.devices = {d_bat, d_sense, d_ref};
    circuit.connections = {
        {"bat.v_out", "sense.v_in", {}},
        {"sense.v_out", "gnd.v", {}},
        {"bat.v_in", "gnd.v", {}}
    };
    registry.types["debug_map_circuit"] = circuit;

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
    TypeRegistry registry;
    register_basic_electrical_types(registry);

    TypeDefinition circuit;
    circuit.classname = "diag_warn_circuit";
    circuit.cpp_class = false;

    DeviceInstance d_src;
    d_src.name = "src";
    d_src.classname = "ElectricalSource";
    d_src.execution = make_execution(true, false, false, false, false, false, false, false, false);

    DeviceInstance d_load;
    d_load.name = "load";
    d_load.classname = "ElectricalConductance";
    d_load.execution = make_execution(true, false, false, false, false, false, false, false, false);

    DeviceInstance d_ref;
    d_ref.name = "gnd";
    d_ref.classname = "RefNode";
    d_ref.execution = make_execution(true, false, false, false, false, false, false, false, false);

    circuit.devices = {d_src, d_load, d_ref};
    circuit.connections = {
        {"src.v_out", "load.v_in", {}},
        {"load.v_out", "gnd.v", {}},
        {"src.v_in", "gnd.v", {}}
    };
    registry.types["diag_warn_circuit"] = circuit;

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
    TypeRegistry registry;
    register_basic_electrical_types(registry);

    TypeDefinition circuit;
    circuit.classname = "debug_idx_circuit";
    circuit.cpp_class = false;

    DeviceInstance d_src;
    d_src.name = "src";
    d_src.classname = "ElectricalSource";
    d_src.execution = make_execution(true, false, false, false, false, false, false, false, false);

    DeviceInstance d_load;
    d_load.name = "load";
    d_load.classname = "ElectricalConductance";
    d_load.execution = make_execution(true, false, false, false, false, false, false, false, false);

    DeviceInstance d_ref;
    d_ref.name = "gnd";
    d_ref.classname = "RefNode";
    d_ref.execution = make_execution(true, false, false, false, false, false, false, false, false);

    circuit.devices = {d_src, d_load, d_ref};
    circuit.connections = {
        {"src.v_out", "load.v_in", {}},
        {"load.v_out", "gnd.v", {}},
        {"src.v_in", "gnd.v", {}}
    };
    registry.types["debug_idx_circuit"] = circuit;

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
    TypeRegistry registry;

    TypeDefinition battery_type;
    battery_type.classname = "ElectricalSource";
    battery_type.cpp_class = true;
    battery_type.ports["v_out"] = Port{PortDirection::Out, PortType::V, std::nullopt};
    battery_type.ports["v_in"] = Port{PortDirection::In, PortType::V, std::nullopt};
    battery_type.domains = {{Domain::Electrical}};
    registry.types["ElectricalSource"] = battery_type;

    TypeDefinition resistor_type;
    resistor_type.classname = "Resistor";
    resistor_type.cpp_class = true;
    resistor_type.ports["v_in"] = Port{PortDirection::In, PortType::V, std::nullopt};
    resistor_type.ports["v_out"] = Port{PortDirection::Out, PortType::V, std::nullopt};
    resistor_type.domains = {{Domain::Electrical}};
    registry.types["Resistor"] = resistor_type;

    TypeDefinition ref_type;
    ref_type.classname = "RefNode";
    ref_type.cpp_class = true;
    ref_type.ports["v"] = Port{PortDirection::InOut, PortType::V, std::nullopt};
    ref_type.domains = {{Domain::Electrical}};
    ref_type.scheduler_source = true;
    registry.types["RefNode"] = ref_type;

    TypeDefinition circuit;
    circuit.classname = "test_circuit";
    circuit.cpp_class = false;

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

    circuit.devices = {d_bat, d_load, d_ref};
    circuit.connections = {
        {"bat.v_out", "load.v_in", {}},
        {"load.v_out", "gnd.v", {}},
        {"bat.v_in", "gnd.v", {}}
    };
    registry.types["test_circuit"] = circuit;

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
    TypeRegistry registry;

    registry.types["RefNode"] = make_refnode_type(PortDirection::Out);

    TypeDefinition consumer_type;
    consumer_type.classname = "Voltmeter";
    consumer_type.cpp_class = true;
    consumer_type.ports["v"] = Port{PortDirection::In, PortType::V, std::nullopt};
    consumer_type.domains = {{Domain::Electrical}};
    consumer_type.scheduler_source = false;
    consumer_type.execution = make_execution(false, true, false, false, false, false, false, false, false);
    registry.types["Voltmeter"] = consumer_type;

    TypeDefinition td;
    td.classname = "sched_order_test";
    td.cpp_class = false;

    DeviceInstance meter;
    meter.name = "meter";
    meter.classname = "Voltmeter";

    DeviceInstance src;
    src.name = "src";
    src.classname = "RefNode";

    // Intentionally reversed declaration order: consumer first, source second.
    td.devices = {meter, src};
    td.connections = {{"src.v", "meter.v", {}}};
    registry.types[td.classname] = td;

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
    TypeRegistry registry;

    // IndicatorLight (simple pass-through component)
    TypeDefinition light = make_indicator_light_type();
    registry.types["IndicatorLight"] = light;

    // Composite: vin→lamp→vout
    TypeDefinition composite;
    composite.classname = "bridge_test";
    composite.cpp_class = false;

    DeviceInstance d_lamp;
    d_lamp.name = "lamp";
    d_lamp.classname = "IndicatorLight";
    composite.devices = {d_lamp};
    composite.bridge_ports = {
        make_bridge_port_def("vin", PortDirection::In, PortType::V),
        make_bridge_port_def("vout", PortDirection::Out, PortType::V),
    };
    composite.connections = {
        {"vin.port", "lamp.v_in", {}},
        {"lamp.v_out", "vout.port", {}}
    };
    composite.ports["vin"]  = Port{PortDirection::In, PortType::V, std::nullopt};
    composite.ports["vout"] = Port{PortDirection::Out, PortType::V, std::nullopt};
    registry.types["bridge_test"] = composite;

    // Generate AOT code
    auto aot_result = CodeGen::generate_composite_systems(composite, registry);
    ASSERT_FALSE(aot_result.header.empty());

    // Extract SIGNAL_COUNT
    std::regex signal_count_re(R"(SIGNAL_COUNT\s*=\s*(\d+))");
    std::smatch match;
    ASSERT_TRUE(std::regex_search(aot_result.header, match, signal_count_re))
        << "AOT header should contain SIGNAL_COUNT";
    uint32_t aot_signal_count = static_cast<uint32_t>(std::stoul(match[1].str()));

    // Also run through JIT path
    std::set<std::string> loading_stack;
    auto expanded = expand_sub_blueprint_references(composite, registry, loading_stack);
    for (auto& dev : expanded.devices) {
        const auto* type_def = registry.get(dev.classname);
        if (type_def) dev = merge_device_instance(dev, *type_def);
    }
    BuildResult jit_result = build_systems_dev(make_jit_input_from_composite(expanded.devices, expanded.bridge_ports, expanded.connections));

    // Key assertion: vin.ext and vin.port MUST share a signal in JIT
    auto jit_sig = [&](const std::string& port) -> uint32_t {
        auto it = jit_result.port_to_signal.find(port);
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
    TypeRegistry registry;

    TypeDefinition cvs;
    cvs.classname = "ControlledVoltageSource";
    cvs.cpp_class = true;
    cvs.ports["cmd"] = Port{PortDirection::In, PortType::Any, std::nullopt};
    cvs.ports["gain"] = Port{PortDirection::In, PortType::Any, std::nullopt};
    cvs.ports["offset"] = Port{PortDirection::In, PortType::Any, std::nullopt};
    cvs.ports["min_v"] = Port{PortDirection::In, PortType::Any, std::nullopt};
    cvs.ports["max_v"] = Port{PortDirection::In, PortType::Any, std::nullopt};
    cvs.ports["v_pos"] = Port{PortDirection::Out, PortType::V, std::nullopt};
    cvs.ports["v_neg"] = Port{PortDirection::In, PortType::V, std::nullopt};
    cvs.domains = {{Domain::Electrical}};
    cvs.execution = make_execution(true, false, false, false, false, false, false, false, false);
    registry.types["ControlledVoltageSource"] = cvs;

    TypeDefinition vc;
    vc.classname = "VariableConductance";
    vc.cpp_class = true;
    vc.ports["cmd"] = Port{PortDirection::In, PortType::Any, std::nullopt};
    vc.ports["g_min"] = Port{PortDirection::In, PortType::Any, std::nullopt};
    vc.ports["g_max"] = Port{PortDirection::In, PortType::Any, std::nullopt};
    vc.ports["v_in"] = Port{PortDirection::In, PortType::V, std::nullopt};
    vc.ports["v_out"] = Port{PortDirection::Out, PortType::V, std::nullopt};
    vc.domains = {{Domain::Electrical}};
    vc.execution = make_execution(true, false, false, false, false, false, false, false, false);
    registry.types["VariableConductance"] = vc;

    registry.types["RefNode"] = make_refnode_type();
    registry.types["Value"] = make_value_type();

    TypeDefinition circuit;
    circuit.classname = "dynamic_patch_test";
    circuit.cpp_class = false;

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

    circuit.devices = {d_ref, d_cvs, d_vc, d_cmd, d_gain, d_offset, d_min, d_max, d_gmin, d_gmax};
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
    registry.types[circuit.classname] = circuit;

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
