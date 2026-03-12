#include <gtest/gtest.h>
#include "editor/data/blueprint.h"
#include "json_parser/json_parser.h"


// ============================================================
// Bake-In: Convert SubBlueprintInstance from reference to embedded
// ============================================================

TEST(BakeIn, SetsBakedInFlag) {
    Blueprint bp;

    Node bat;
    bat.id = "bat_main";
    bat.type_name = "Battery";
    bp.add_node(bat);

    Node vin;
    vin.id = "lamp_1:vin";
    vin.type_name = "BlueprintInput";
    vin.group_id = "lamp_1";
    bp.add_node(vin);

    Node lamp;
    lamp.id = "lamp_1:lamp";
    lamp.type_name = "IndicatorLight";
    lamp.group_id = "lamp_1";
    lamp.params["color"] = "red";
    bp.add_node(lamp);

    Node collapsed;
    collapsed.id = "lamp_1";
    collapsed.type_name = "lamp_pass_through";
    collapsed.expandable = true;
    collapsed.collapsed = true;
    collapsed.pos = {400.0f, 300.0f};
    collapsed.size = {120.0f, 80.0f};
    bp.add_node(collapsed);

    SubBlueprintInstance sbi;
    sbi.id = "lamp_1";
    sbi.blueprint_path = "library/systems/lamp_pass_through.json";
    sbi.type_name = "lamp_pass_through";
    sbi.pos = {400.0f, 300.0f};
    sbi.size = {120.0f, 80.0f};
    sbi.baked_in = false;
    sbi.params_override["lamp.color"] = "green";
    bp.sub_blueprint_instances.push_back(sbi);

    bool result = bp.bake_in_sub_blueprint("lamp_1");
    ASSERT_TRUE(result);

    ASSERT_EQ(bp.sub_blueprint_instances.size(), 1u);
    EXPECT_TRUE(bp.sub_blueprint_instances[0].baked_in);
    EXPECT_EQ(bp.sub_blueprint_instances[0].id, "lamp_1");
    EXPECT_EQ(bp.sub_blueprint_instances[0].blueprint_path,
              "library/systems/lamp_pass_through.json");

    EXPECT_TRUE(bp.sub_blueprint_instances[0].params_override.empty());
    EXPECT_TRUE(bp.sub_blueprint_instances[0].layout_override.empty());
    EXPECT_TRUE(bp.sub_blueprint_instances[0].internal_routing.empty());

    EXPECT_EQ(bp.sub_blueprint_instances[0].internal_node_ids.size(), 2u);

    EXPECT_NE(bp.find_node("lamp_1:vin"), nullptr);
    EXPECT_NE(bp.find_node("lamp_1:lamp"), nullptr);

    auto* cnode = bp.find_node("lamp_1");
    ASSERT_NE(cnode, nullptr);
    EXPECT_TRUE(cnode->expandable);
}

TEST(BakeIn, FlattensParamOverrides) {
    Blueprint bp;

    Node lamp;
    lamp.id = "lamp_1:lamp";
    lamp.type_name = "IndicatorLight";
    lamp.group_id = "lamp_1";
    lamp.params["color"] = "red";
    bp.add_node(lamp);

    Node collapsed;
    collapsed.id = "lamp_1";
    collapsed.expandable = true;
    collapsed.collapsed = true;
    bp.add_node(collapsed);

    SubBlueprintInstance sbi;
    sbi.id = "lamp_1";
    sbi.type_name = "lamp_pass_through";
    sbi.params_override["lamp.color"] = "green";
    bp.sub_blueprint_instances.push_back(sbi);

    bp.bake_in_sub_blueprint("lamp_1");

    auto* lamp_node = bp.find_node("lamp_1:lamp");
    ASSERT_NE(lamp_node, nullptr);
    EXPECT_EQ(lamp_node->params["color"], "green");
}

TEST(BakeIn, FlattensLayoutOverrides) {
    Blueprint bp;

    Node vin;
    vin.id = "lamp_1:vin";
    vin.type_name = "BlueprintInput";
    vin.group_id = "lamp_1";
    vin.pos = {0.0f, 0.0f};
    bp.add_node(vin);

    Node collapsed;
    collapsed.id = "lamp_1";
    collapsed.expandable = true;
    collapsed.collapsed = true;
    bp.add_node(collapsed);

    SubBlueprintInstance sbi;
    sbi.id = "lamp_1";
    sbi.type_name = "lamp_pass_through";
    sbi.layout_override["vin"] = {350.0f, 300.0f};
    bp.sub_blueprint_instances.push_back(sbi);

    bp.bake_in_sub_blueprint("lamp_1");

    auto* vin_node = bp.find_node("lamp_1:vin");
    ASSERT_NE(vin_node, nullptr);
    EXPECT_FLOAT_EQ(vin_node->pos.x, 350.0f);
    EXPECT_FLOAT_EQ(vin_node->pos.y, 300.0f);
}

TEST(BakeIn, NonexistentId_ReturnsFalse) {
    Blueprint bp;
    EXPECT_FALSE(bp.bake_in_sub_blueprint("nonexistent"));
}
