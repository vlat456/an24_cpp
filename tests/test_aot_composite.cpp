#include <gtest/gtest.h>
#include "codegen/codegen.h"
#include "json_parser/json_parser.h"


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
    // This test will require both JIT and AOT to run the same composite
    // and compare output signals. Detailed implementation depends on
    // SimulationState setup — placeholder for now.
    //
    // Setup:
    //   1. Create composite with known devices + connections
    //   2. Run N steps via JIT (expand_sub_blueprint_references + JIT solver)
    //   3. Compile AOT Systems (or use pre-generated)
    //   4. Run N steps via AOT
    //   5. Compare all signal values within tolerance
    //
    // EXPECT_NEAR(jit_signals[i], aot_signals[i], 1e-6f);
    GTEST_SKIP() << "Requires compiled AOT output — implement after code generation works";
}
