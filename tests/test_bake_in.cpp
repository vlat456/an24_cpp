#include <gtest/gtest.h>
#include "editor/data/blueprint.h"
#include "json_parser/json_parser.h"
#include "ui/core/interned_id.h"

namespace ui {
inline std::ostream& operator<<(std::ostream& os, InternedId id) {
    return os << "InternedId(" << id.raw() << ")";
}
}


// ============================================================
// Bake-In: Convert SubBlueprintInstance from reference to embedded
// ============================================================

TEST(BakeIn, SetsBakedInFlag) {
    Blueprint bp;
    auto& I = bp.interner();

    Node bat;
    bat.id = I.intern("bat_main");
    bat.type_name = "Battery";
    bp.add_node(bat);

    Node vin;
    vin.id = I.intern("lamp_1:vin");
    vin.type_name = "BlueprintInput";
    vin.group_id = "lamp_1";
    bp.add_node(vin);

    Node lamp;
    lamp.id = I.intern("lamp_1:lamp");
    lamp.type_name = "IndicatorLight";
    lamp.group_id = "lamp_1";
    lamp.params["color"] = "red";
    bp.add_node(lamp);

    Node collapsed;
    collapsed.id = I.intern("lamp_1");
    collapsed.type_name = "lamp_pass_through";
    collapsed.expandable = true;
    collapsed.collapsed = true;
    collapsed.pos = {400.0f, 300.0f};
    collapsed.set_explicit_size(ui::Pt(120.0f, 80.0f));
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
    auto& I = bp.interner();

    Node lamp;
    lamp.id = I.intern("lamp_1:lamp");
    lamp.type_name = "IndicatorLight";
    lamp.group_id = "lamp_1";
    lamp.params["color"] = "red";
    bp.add_node(lamp);

    Node collapsed;
    collapsed.id = I.intern("lamp_1");
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
    auto& I = bp.interner();

    Node vin;
    vin.id = I.intern("lamp_1:vin");
    vin.type_name = "BlueprintInput";
    vin.group_id = "lamp_1";
    vin.pos = {0.0f, 0.0f};
    bp.add_node(vin);

    Node collapsed;
    collapsed.id = I.intern("lamp_1");
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

// ============================================================
// Round-trip: Embedded nodes only in sub_blueprints[].nodes
// must be restored into bp.nodes on load.
// This reproduces the data loss bug where only hand-added
// elements survived save/load of a baked-in sub-blueprint.
// ============================================================

TEST(BakeIn, RoundTrip_EmbeddedNodesRestoredFromSubBlueprint) {
    // Simulate the exact failure scenario from GSC.blueprint:
    // - Sub-blueprint "rug_82_1_1" is baked-in (editable)
    // - It has 5 embedded nodes: Comp, lut_2, pid_1, refnode_4, v
    // - Only pid_1 and refnode_4 (hand-added) are in top-level nodes
    // - Comp, lut_2, v are ONLY in sub_blueprints["rug_82_1_1"].nodes
    //
    // On load, ALL 5 nodes must be restored.

    std::string json = R"({
        "meta": { "name": "RoundTripTest" },
        "version": 2,
        "nodes": {
            "top_node": {
                "type": "Add",
                "pos": [100.0, 100.0]
            },
            "pid_1": {
                "type": "PID",
                "group_id": "sub_1",
                "pos": [400.0, 64.0],
                "params": { "Kp": "0.4" }
            },
            "refnode_4": {
                "type": "RefNode",
                "group_id": "sub_1",
                "render_hint": "ref",
                "pos": [256.0, 16.0],
                "params": { "value": "28.5" }
            }
        },
        "sub_blueprints": {
            "sub_1": {
                "template": "systems/RUG_82_1",
                "type_name": "RUG_82_1",
                "pos": [768.0, 48.0],
                "size": [120.0, 96.0],
                "collapsed": true,
                "nodes": {
                    "Comp": {
                        "type": "BlueprintOutput",
                        "group_id": "sub_1",
                        "pos": [896.0, 128.0],
                        "params": {
                            "exposed_direction": "Out",
                            "exposed_type": "Any"
                        }
                    },
                    "lut_2": {
                        "type": "LUT",
                        "group_id": "sub_1",
                        "pos": [576.0, 96.0],
                        "params": {
                            "table": "0.0:50.0; 0.5:10.0; 1.0:0.1"
                        }
                    },
                    "pid_1": {
                        "type": "PID",
                        "group_id": "sub_1",
                        "pos": [400.0, 64.0],
                        "params": { "Kp": "0.4" }
                    },
                    "refnode_4": {
                        "type": "RefNode",
                        "group_id": "sub_1",
                        "render_hint": "ref",
                        "pos": [256.0, 16.0],
                        "params": { "value": "28.5" }
                    },
                    "v": {
                        "type": "BlueprintInput",
                        "group_id": "sub_1",
                        "pos": [80.0, 80.0],
                        "params": {
                            "exposed_direction": "In",
                            "exposed_type": "V"
                        }
                    }
                },
                "wires": [
                    {
                        "id": "w_lut_comp",
                        "from": ["lut_2", "output"],
                        "to": ["Comp", "port"]
                    },
                    {
                        "id": "w_pid_lut",
                        "from": ["pid_1", "output"],
                        "to": ["lut_2", "input"]
                    },
                    {
                        "id": "w_v_pid",
                        "from": ["v", "port"],
                        "to": ["pid_1", "feedback"]
                    },
                    {
                        "id": "w_ref_pid",
                        "from": ["refnode_4", "v"],
                        "to": ["pid_1", "setpoint"]
                    }
                ]
            }
        },
        "wires": [
            {
                "id": "wire_top",
                "from": ["top_node", "o"],
                "to": ["sub_1", "v"]
            }
        ]
    })";

    auto loaded = Blueprint::deserialize(json);
    ASSERT_TRUE(loaded.has_value());

    // Verify ALL 5 embedded nodes are restored (not just the 2 in top-level)
    // pid_1 and refnode_4 are in top-level nodes with group_id="sub_1"
    // Comp, lut_2, v should be injected from sub_blueprints["sub_1"].nodes

    // The hand-added nodes (unprefixed, already in top-level)
    EXPECT_NE(loaded->find_node("pid_1"), nullptr)
        << "pid_1 (hand-added, top-level) should survive";
    EXPECT_NE(loaded->find_node("refnode_4"), nullptr)
        << "refnode_4 (hand-added, top-level) should survive";

    // The library-origin nodes (ONLY in sub_blueprints[].nodes)
    // They should be injected with prefixed IDs: sub_1:Comp, sub_1:lut_2, sub_1:v
    EXPECT_NE(loaded->find_node("sub_1:Comp"), nullptr)
        << "Comp should be restored from embedded sub-blueprint nodes";
    EXPECT_NE(loaded->find_node("sub_1:lut_2"), nullptr)
        << "lut_2 should be restored from embedded sub-blueprint nodes";
    EXPECT_NE(loaded->find_node("sub_1:v"), nullptr)
        << "v should be restored from embedded sub-blueprint nodes";

    // Verify group_id is correct on all embedded nodes
    if (auto* comp = loaded->find_node("sub_1:Comp")) {
        EXPECT_EQ(comp->group_id, "sub_1");
        EXPECT_EQ(comp->type_name, "BlueprintOutput");
    }
    if (auto* lut = loaded->find_node("sub_1:lut_2")) {
        EXPECT_EQ(lut->group_id, "sub_1");
        EXPECT_EQ(lut->type_name, "LUT");
    }
    if (auto* v = loaded->find_node("sub_1:v")) {
        EXPECT_EQ(v->group_id, "sub_1");
        EXPECT_EQ(v->type_name, "BlueprintInput");
    }

    // Verify the SBI has all 5 internal_node_ids
    ASSERT_EQ(loaded->sub_blueprint_instances.size(), 1u);
    const auto& sbi = loaded->sub_blueprint_instances[0];
    EXPECT_TRUE(sbi.baked_in);
    EXPECT_EQ(sbi.internal_node_ids.size(), 5u)
        << "All 5 embedded nodes should be in internal_node_ids";

    // Verify embedded wires were also restored
    // We should have the top-level wire + 4 internal wires = 5 total
    EXPECT_GE(loaded->wires.size(), 5u)
        << "Embedded wires should be restored along with embedded nodes";

    // The top-level node should also be present
    EXPECT_NE(loaded->find_node("top_node"), nullptr);
}

TEST(BakeIn, RoundTrip_AllNodesInTopLevel_NoDoubling) {
    // When ALL embedded nodes are already in the top-level nodes map
    // (the normal save path), they should NOT be duplicated.
    std::string json = R"({
        "meta": { "name": "NoDupTest" },
        "version": 2,
        "nodes": {
            "sub_1:lamp": {
                "type": "IndicatorLight",
                "group_id": "sub_1",
                "pos": [200.0, 200.0]
            },
            "sub_1:vin": {
                "type": "BlueprintInput",
                "group_id": "sub_1",
                "pos": [100.0, 100.0]
            }
        },
        "sub_blueprints": {
            "sub_1": {
                "template": "systems/lamp_pass_through",
                "type_name": "lamp_pass_through",
                "pos": [0.0, 0.0],
                "size": [120.0, 80.0],
                "collapsed": true,
                "nodes": {
                    "lamp": {
                        "type": "IndicatorLight",
                        "group_id": "sub_1",
                        "pos": [200.0, 200.0]
                    },
                    "vin": {
                        "type": "BlueprintInput",
                        "group_id": "sub_1",
                        "pos": [100.0, 100.0]
                    }
                },
                "wires": []
            }
        },
        "wires": []
    })";

    auto loaded = Blueprint::deserialize(json);
    ASSERT_TRUE(loaded.has_value());

    // Count nodes with group_id "sub_1" - should be exactly 2, not 4
    int group_count = 0;
    for (const auto& n : loaded->nodes) {
        if (n.group_id == "sub_1") group_count++;
    }
    EXPECT_EQ(group_count, 2)
        << "Embedded nodes already in top-level should not be duplicated";
}
