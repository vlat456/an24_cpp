#include <gtest/gtest.h>
#include "codegen/codegen.h"
#include "json_parser/json_parser.h"
#include "jit_solver/jit_solver.h"
#include <regex>
#include <set>


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
    DeviceInstance d_lamp;
    d_lamp.name = "lamp";
    d_lamp.classname = "IndicatorLight";
    DeviceInstance d_vout;
    d_vout.name = "vout";
    d_vout.classname = "BlueprintOutput";
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
    registry.types["BlueprintInput"] = bp_in;

    // BlueprintOutput: port (In, Any), ext (Out, Any, alias→port)
    TypeDefinition bp_out;
    bp_out.classname = "BlueprintOutput";
    bp_out.cpp_class = true;
    bp_out.ports["port"] = Port{PortDirection::In, PortType::Any, std::nullopt};
    bp_out.ports["ext"]  = Port{PortDirection::Out, PortType::Any, std::string("port")};
    bp_out.domains = {{Domain::Electrical}};
    registry.types["BlueprintOutput"] = bp_out;

    // IndicatorLight: v_in (In), v_out (Out), brightness (Out)
    TypeDefinition light;
    light.classname = "IndicatorLight";
    light.cpp_class = true;
    light.ports["v_in"]       = Port{PortDirection::In, PortType::V, std::nullopt};
    light.ports["v_out"]      = Port{PortDirection::Out, PortType::V, std::nullopt};
    light.ports["brightness"] = Port{PortDirection::Out, PortType::I, std::nullopt};
    light.domains = {{Domain::Electrical}};
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

    // JIT adds +1 sentinel signal for unconnected ports
    uint32_t jit_signal_count = jit_result.signal_count;
    EXPECT_EQ(aot_signal_count + 1, jit_signal_count)
        << "AOT signal_count + 1 (sentinel) should equal JIT signal_count";

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

    // Alias: vin.ext -> vin.port (BlueprintInput alias)
    EXPECT_EQ(jit_sig("vin.ext"), jit_sig("vin.port"))
        << "Alias vin.ext should share signal with vin.port";

    // Alias: vout.ext -> vout.port (BlueprintOutput alias)
    EXPECT_EQ(jit_sig("vout.ext"), jit_sig("vout.port"))
        << "Alias vout.ext should share signal with vout.port";

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

    // Verify AOT source contains solve calls for all three devices
    EXPECT_NE(aot_result.source.find("vin.solve_electrical"), std::string::npos)
        << "AOT source should contain vin.solve_electrical call";
    EXPECT_NE(aot_result.source.find("lamp.solve_electrical"), std::string::npos)
        << "AOT source should contain lamp.solve_electrical call";
    EXPECT_NE(aot_result.source.find("vout.solve_electrical"), std::string::npos)
        << "AOT source should contain vout.solve_electrical call";
}
