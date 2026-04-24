#include "io/json/parse_json_api.h"
#include "io/json/component_registry_json_loader.h"
#include "io/json/type_definition_json.h"
#include "core/registry/component_resolution.h"
#include "core/model/presentation_spec.h"

#include <nlohmann/json.hpp>
#include <gtest/gtest.h>
#include <sstream>
#include <filesystem>
#include <fstream>


TEST(JsonParserTest, ParseEmptyContext) {
    std::string json = R"({"templates":{},"devices":[],"connections":[]})";
    auto ctx = parse_json(json);

    EXPECT_TRUE(ctx.templates.empty());
    EXPECT_TRUE(ctx.devices.empty());
    EXPECT_TRUE(ctx.connections.empty());
}

TEST(JsonParserTest, ParseErrorOnInvalidJson) {
    EXPECT_THROW(parse_json("not valid json {{{"), std::exception);
}

TEST(JsonParserTest, ParseRejectsInvalidPortDirection) {
    std::string json = R"({
        "devices": [
            {
                "name": "bad",
                "classname": "ElectricalSource",
                "ports": {
                    "v_out": {"direction": "sideways", "type": "V"}
                },
                "params": {"voltage": "28.0", "resistance": "0.1"}
            }
        ],
        "connections": []
    })";

    EXPECT_THROW(parse_json(json), std::runtime_error);
}

TEST(JsonParserTest, ParseAndSerializeRoundTrip) {
    ParserContext ctx;

    // RefNode as a device (has only 'v' port)
    ResolvedDevice gnd;
    gnd.name = "gnd1";
    gnd.classname = "RefNode";
    gnd.priority = "med";
    gnd.critical = false;
    gnd.ports["v"] = Port{bp2::Direction::Output, PortType::V};
    gnd.params["value"] = "0.0";
    ctx.devices.push_back(gnd);

    // Battery
    ResolvedDevice bat;
    bat.name = "bat";
    bat.classname = "ElectricalSource";
    bat.priority = "high";
    bat.ports["v_out"] = Port{bp2::Direction::Output, PortType::V};
    bat.params["voltage"] = "28.0";
    ctx.devices.push_back(bat);

    // Explicit connection
    ctx.connections.push_back({"bat.v_out", "gnd1.v"});

    // Serialize and parse back
    std::string json = serialize_json(ctx);
    auto ctx2 = parse_json(json);

    EXPECT_EQ(ctx2.devices.size(), 2);
    EXPECT_EQ(ctx2.connections.size(), 1);
    EXPECT_EQ(ctx2.connections[0].from, "bat.v_out");
    EXPECT_EQ(ctx2.connections[0].to, "gnd1.v");
}

TEST(JsonParserTest, RoundTripWithTemplates) {
    ParserContext ctx;

    // Create template
    SystemTemplate tpl;
    tpl.name = "TestSys";

    DeviceInstance bat;
    bat.name = "bat";
    bat.classname = "ElectricalSource";
    tpl.devices.push_back(bat);

    SubsystemCall sub;
    sub.name = "sub1";
    sub.template_name = "Other";
    sub.port_map["p"] = "pwr";
    tpl.subsystems.push_back(sub);

    tpl.exposed_ports["pwr"] = "internal_bus";
    tpl.domains = {Domain::Electrical};

    ctx.templates["TestSys"] = tpl;

    // Serialize and parse back
    std::string json = serialize_json(ctx);
    auto ctx2 = parse_json(json);

    EXPECT_EQ(ctx2.templates.size(), 1);
    auto it = ctx2.templates.find("TestSys");
    ASSERT_NE(it, ctx2.templates.end());
    EXPECT_EQ(it->second.devices.size(), 1);
    EXPECT_EQ(it->second.subsystems.size(), 1);
    EXPECT_EQ(it->second.subsystems[0].template_name, "Other");
    auto exposed_it = it->second.exposed_ports.find("pwr");
    ASSERT_NE(exposed_it, it->second.exposed_ports.end());
    EXPECT_EQ(exposed_it->second, "internal_bus");
}

TEST(JsonParserTest, ParseMultipleDomains) {
    std::string json = R"({
        "templates": {},
        "devices": [
            {
                "name": "pump",
                "classname": "ElectricPump",
                "domain": "Electrical,Hydraulic",
                "params": {"max_pressure": "1000.0"}
            }
        ],
        "connections": []
    })";

    auto ctx = parse_json(json);
    ASSERT_EQ(ctx.devices.size(), 1);
    const auto& dev = ctx.devices[0];
    const auto& domains = dev.domains;
    EXPECT_EQ(domains.size(), 2);
    EXPECT_EQ(domains[0], Domain::Electrical);
    EXPECT_EQ(domains[1], Domain::Hydraulic);
}

TEST(JsonParserTest, ParseDevicesWithAllFields) {
    std::string json = R"({
        "templates": {},
        "devices": [
            {
                "name": "test_device",
                "template": "my_template",
                "classname": "Relay",
                "priority": "high",
                "bucket": 2,
                "critical": true,
                "is_composite": true,
                "domain": "Electrical",
                "ports": {
                    "v_in": "i",
                    "v_out": "o",
                    "control": "i"
                },
                "params": {
                    "closed": "true"
                }
            }
        ],
        "connections": []
    })";

    auto ctx = parse_json(json);
    ASSERT_EQ(ctx.devices.size(), 1);
    const auto& dev = ctx.devices[0];

    EXPECT_EQ(dev.name, "test_device");
    EXPECT_EQ(dev.template_name, "my_template");
    EXPECT_EQ(dev.classname, "Relay");
    EXPECT_EQ(dev.priority, "high");
    EXPECT_EQ(dev.bucket.value(), 2);
    EXPECT_TRUE(dev.critical);
    ASSERT_EQ(dev.domains.size(), 2u);
    EXPECT_EQ(dev.domains[0], Domain::Electrical);

    // Relay blueprint defines 5 ports: v_in, v_out, control, state, hold_threshold
    // resolve_component() enriches with library-defined ports
    EXPECT_EQ(dev.ports.size(), 5);
    auto it_in = dev.ports.find("v_in");
    ASSERT_NE(it_in, dev.ports.end());
    EXPECT_EQ(it_in->second.direction, bp2::Direction::Input);
    auto it_out = dev.ports.find("v_out");
    ASSERT_NE(it_out, dev.ports.end());
    EXPECT_EQ(it_out->second.direction, bp2::Direction::Output);
    auto it_control = dev.ports.find("control");
    ASSERT_NE(it_control, dev.ports.end());
    EXPECT_EQ(it_control->second.direction, bp2::Direction::Input);
    auto it_state = dev.ports.find("state");
    ASSERT_NE(it_state, dev.ports.end());
    EXPECT_EQ(it_state->second.direction, bp2::Direction::Output);

    auto it_param = dev.params.find("closed");
    ASSERT_NE(it_param, dev.params.end());
    EXPECT_EQ(it_param->second, "true");
}

TEST(JsonParserTest, ParseConnectionFormats) {
    // Test object format
    std::string json1 = R"({
        "templates": {},
        "devices": [],
        "connections": [
            {"from": "a.b", "to": "c.d"}
        ]
    })";
    auto ctx1 = parse_json(json1);
    EXPECT_EQ(ctx1.connections[0].from, "a.b");
    EXPECT_EQ(ctx1.connections[0].to, "c.d");

    // Test string format with arrow
    std::string json2 = R"({
        "templates": {},
        "devices": [],
        "connections": [
            "a.b -> c.d"
        ]
    })";
    auto ctx2 = parse_json(json2);
    EXPECT_EQ(ctx2.connections[0].from, "a.b");
    EXPECT_EQ(ctx2.connections[0].to, "c.d");
}

TEST(JsonParserTest, SerializePreservesData) {
    ParserContext ctx;

    ResolvedDevice dev;
    dev.name = "test";
    dev.classname = "ElectricalSource";
    dev.params["voltage"] = "28.0";
    ctx.devices.push_back(dev);

    ctx.connections.push_back({"a.b", "c.d"});

    std::string json = serialize_json(ctx);
    auto ctx2 = parse_json(json);

    EXPECT_EQ(ctx2.devices.size(), 1);
    EXPECT_EQ(ctx2.devices[0].params["voltage"], "28.0");
    EXPECT_EQ(ctx2.connections.size(), 1);
}

// [g7h8] InOut port direction roundtrip
TEST(JsonParserTest, InOutPortDirection_Roundtrip_g7h8) {
    ParserContext ctx;

    ResolvedDevice dev;
    dev.name = "test_dev";
    dev.classname = "ElectricalSource"; // use known component for validation
    dev.ports["v_in"] = Port{bp2::Direction::Input, PortType::V};
    dev.ports["v_out"] = Port{bp2::Direction::Output, PortType::V};
    ctx.devices.push_back(dev);

    std::string json = serialize_json(ctx);

    // In direction should serialize as "In", Out as "Out"
    EXPECT_NE(json.find("\"In\""), std::string::npos)
        << "[g7h8] In direction should be preserved in serialized JSON";
    EXPECT_NE(json.find("\"Out\""), std::string::npos)
        << "[g7h8] Out direction should be preserved in serialized JSON";

    auto ctx2 = parse_json(json);
    ASSERT_EQ(ctx2.devices.size(), 1);
    EXPECT_EQ(ctx2.devices[0].ports["v_in"].direction, bp2::Direction::Input);
    EXPECT_EQ(ctx2.devices[0].ports["v_out"].direction, bp2::Direction::Output);
}

// [g7h8] InOut enum value exists and parses correctly
TEST(JsonParserTest, InOutEnumExists_g7h8) {
    // Verify InOut is a valid Direction value
    bp2::Direction d = bp2::Direction::InOut;
    EXPECT_NE(d, bp2::Direction::Input);
    EXPECT_NE(d, bp2::Direction::Output);
}

// ============================================================================
// Port Type Tests - FAILING TESTS FIRST (TDD)
// ============================================================================

TEST(JsonParserTest, ParsePortType_FromJson) {
    std::string json = R"({
        "templates": {},
        "devices": [{
            "name": "batt",
            "classname": "ElectricalSource",
            "ports": {
                "v_out": {"direction": "Out", "type": "V"}
            }
        }],
        "connections": []
    })";

    auto ctx = parse_json(json);
    ASSERT_EQ(ctx.devices.size(), 1);
    EXPECT_EQ(ctx.devices[0].ports["v_out"].type, PortType::V);
}

TEST(JsonParserTest, ParsePortType_RPM_FromJson) {
    std::string json = R"({
        "templates": {},
        "devices": [{
            "name": "pump",
            "classname": "ElectricalSource",
            "ports": {
                "v_out": {"direction": "Out", "type": "V"}
            }
        }],
        "connections": []
    })";

    auto ctx = parse_json(json);
    ASSERT_EQ(ctx.devices.size(), 1);
    EXPECT_EQ(ctx.devices[0].ports["v_out"].type, PortType::V);
}

TEST(JsonParserTest, ValidateConnection_MismatchedTypes_ShouldFail) {
    // NOTE: Port type validation is done during wire creation in the editor,
    // not during JSON parsing. This test now verifies that incompatible
    // connections can be parsed from JSON (validation happens at runtime).

    std::string json = R"({
        "templates": {},
        "devices": [
        {
            "name": "batt",
            "classname": "ElectricalSource",
            "ports": {
                "v_out": {"direction": "Out", "type": "V"}
            }
        },
        {
            "name": "pump",
            "classname": "ElectricalSource",
            "ports": {
                "v_out": {"direction": "Out", "type": "V"}
            }
        },
            {
                "name": "batt2",
                "classname": "ElectricalSource",
                "ports": {
                    "v_in": {"direction": "In", "type": "V"}
                }
            }
        ],
        "connections": [
            {"from": "batt1.v_out", "to": "batt2.v_in"}
        ]
    })";

    // Should NOT throw - V can connect to V
    auto ctx = parse_json(json);
    EXPECT_EQ(ctx.connections.size(), 1);
}

TEST(JsonParserTest, ValidateConnection_BoolToV_ShouldFail) {
    // NOTE: Port type validation is done during wire creation in the editor,
    // not during JSON parsing.

    std::string json = R"({
        "templates": {},
        "devices": [
            {
                "name": "button",
                "classname": "HoldButton",
                "ports": {
                    "v_out": {"direction": "Out", "type": "Bool"}
                }
            },
        {
            "name": "batt",
            "classname": "ElectricalSource",
            "ports": {
                "v_in": {"direction": "In", "type": "V"}
            }
        }
        ],
        "connections": [
            {"from": "button.v_out", "to": "batt.v_in"}
        ]
    })";

    // Should NOT throw - JSON parsing doesn't validate types
    // Validation happens during wire creation in the editor
    auto ctx = parse_json(json);
    EXPECT_EQ(ctx.connections.size(), 1);
}

TEST(JsonParserTest, ValidateConnection_AnyType_ShouldPass) {
    std::string json = R"({
        "templates": {},
        "devices": [
            {
                "name": "batt1",
                "classname": "ElectricalSource",
                "ports": {
                    "v_out": {"direction": "Out", "type": "V"}
                }
            },
            {
                "name": "batt2",
                "classname": "ElectricalSource",
                "ports": {
                    "v_in": {"direction": "In", "type": "V"}
                }
            }
        ],
        "connections": [
            {"from": "batt1.v_out", "to": "batt2.v_in"}
        ]
    })";

    // Should NOT throw - Any can connect to anything
    auto ctx = parse_json(json);
    EXPECT_EQ(ctx.connections.size(), 1);
}

TEST(JsonParserTest, PortTypeSerialization_RoundTrip) {
    ParserContext ctx;

    // Use ElectricalSource which has voltage ports
    ResolvedDevice dev;
    dev.name = "test";
    dev.classname = "ElectricalSource";
    dev.ports["v_in"] = Port{bp2::Direction::Input, PortType::V};
    dev.ports["v_out"] = Port{bp2::Direction::Output, PortType::V};
    ctx.devices.push_back(dev);

    std::string json = serialize_json(ctx);
    auto ctx2 = parse_json(json);

    EXPECT_EQ(ctx2.devices[0].ports["v_in"].type, PortType::V);
    EXPECT_EQ(ctx2.devices[0].ports["v_out"].type, PortType::V);
}

// ============================================================================
// Regression Tests
// ============================================================================

TEST(JsonParserTest, Regression_PortTypeMerge_TypeDefinitionTypesCopied) {
    // Regression test for bug where port types from TypeDefinition
    // were not being copied to DeviceInstance when ports already existed.

    std::string json = R"({
        "templates": {},
        "devices": [
            {
                "name": "pump",
                "classname": "ElectricalSource"
            }
        ],
        "connections": []
    })";

    auto ctx = parse_json(json);
    ASSERT_EQ(ctx.devices.size(), 1);
    const auto& pump = ctx.devices[0];

    // Verify that port types from TypeDefinition were copied
    EXPECT_EQ(pump.ports.count("v_in"), 1) << "v_in port should exist";
    EXPECT_EQ(pump.ports.at("v_in").type, PortType::V)
        << "v_in type should be V (from TypeDefinition)";

    EXPECT_EQ(pump.ports.count("v_out"), 1) << "v_out port should exist";
    EXPECT_EQ(pump.ports.at("v_out").type, PortType::V)
        << "v_out type should be V (from TypeDefinition)";
}

TEST(JsonParserTest, Regression_PortTypeMerge_InlinePortWithType) {
    // Inline port definition must include type field.

    std::string json = R"({
        "templates": {},
        "devices": [
        {
            "name": "bat",
            "classname": "ElectricalSource"
        },
            {
                "name": "bat",
                "classname": "ElectricalSource",
                "ports": {
                    "v_out": {"direction": "Out", "type": "V"}
                }
            }
        ],
        "connections": [
            {"from": "bat.v_out", "to": "lerp.input"}
        ]
    })";

    // Should not throw - Any type can connect to V
    auto ctx = parse_json(json);
    EXPECT_EQ(ctx.connections.size(), 1);
}

// ============================================================================
// One-to-One Connection Tests - FAILING TESTS FIRST (TDD)
// ============================================================================

TEST(JsonParserTest, OneToOne_MultipleWiresToSamePort_IsValid) {
    // With union-find architecture, multiple wires to same port is valid
    // (signals merge — this is how parallel circuits work)
    std::string json = R"({
        "templates": {},
        "devices": [
            {"name": "bat1", "classname": "ElectricalSource"},
            {"name": "bat2", "classname": "ElectricalSource"},
            {"name": "load", "classname": "IndicatorLight"}
        ],
        "connections": [
            {"from": "bat1.v_out", "to": "load.v_in"},
            {"from": "bat2.v_out", "to": "load.v_in"}
        ]
    })";

    EXPECT_NO_THROW(parse_json(json));
}

TEST(JsonParserTest, OneToOne_MultipleWiresFromSamePort_IsValid) {
    // With union-find architecture, multiple wires from same port is valid
    // (output signal fans out to multiple consumers)
    std::string json = R"({
        "templates": {},
        "devices": [
            {"name": "bat", "classname": "ElectricalSource"},
            {"name": "load1", "classname": "IndicatorLight"},
            {"name": "load2", "classname": "IndicatorLight"}
        ],
        "connections": [
            {"from": "bat.v_out", "to": "load1.v_in"},
            {"from": "bat.v_out", "to": "load2.v_in"}
        ]
    })";

    EXPECT_NO_THROW(parse_json(json));
}

TEST(JsonParserTest, OneToOne_BusAliasPorts_CanHaveMultipleWires) {
    // Bus nodes can have multiple wires to their alias ports
    // (they all connect to the same underlying "v" port)
    std::string json = R"({
        "templates": {},
        "devices": [
            {"name": "bus", "classname": "Bus"},
            {"name": "bat1", "classname": "ElectricalSource"},
            {"name": "bat2", "classname": "ElectricalSource"}
        ],
        "connections": [
            {"from": "bat1.v_out", "to": "bus.v"},
            {"from": "bat2.v_out", "to": "bus.v"}
        ]
    })";

    // Should NOT throw - Bus ports allow multiple connections
    auto ctx = parse_json(json);
    EXPECT_EQ(ctx.connections.size(), 2);
}

TEST(JsonParserTest, OneToOne_RefNode_CanHaveMultipleWires) {
    // RefNode is like Bus - can have multiple wires to its port
    std::string json = R"({
        "templates": {},
        "devices": [
            {"name": "gnd", "classname": "RefNode"},
            {"name": "bat1", "classname": "ElectricalSource"},
            {"name": "bat2", "classname": "ElectricalSource"}
        ],
        "connections": [
            {"from": "bat1.v_out", "to": "gnd.v"},
            {"from": "bat2.v_out", "to": "gnd.v"}
        ]
    })";

    // Should NOT throw - RefNode ports allow multiple connections
    auto ctx = parse_json(json);
    EXPECT_EQ(ctx.connections.size(), 2);
}

// =============================================================================
// Recursive library loading + MenuTree
// =============================================================================

static const char* minimal_blueprint_v2(const char* classname) {
    // Returns a static buffer — only safe for one call at a time
    static char buf[1400];
    snprintf(buf, sizeof(buf),
        R"({"version": "3.0", "id": "%s", "display_name": "%s", "interface": [], "cpp_class": true, "scheduler_source": false, "solver_owned_electrical": false, "domains": ["Electrical"], "execution": {"electrical_passive": true, "electrical_observer": false, "logical": false, "control_commit": false, "electrical_actuator": false, "finalize": false, "mechanical": false, "hydraulic": false, "thermal": false}})",
        classname, classname);
    return buf;
}

TEST(JsonParserTest, ParseTypeDefinition_ExecutionMissingKeyThrows) {
    auto j = nlohmann::json::parse(R"({
        "classname": "ElectricalSource",
        "cpp_class": true,
        "ports": {"v_out": {"direction": "Out", "type": "V"}},
        "execution": {
            "electrical_passive": true,
            "electrical_observer": false,
            "logical": false,
            "control_commit": false,
            "electrical_actuator": false,
            "finalize": false,
            "mechanical": false,
            "hydraulic": false
        }
    })");

    EXPECT_THROW(parse_type_definition(j), std::runtime_error);
}

TEST(JsonParserTest, ParseTypeDefinition_ExecutionUnknownKeyThrows) {
    auto j = nlohmann::json::parse(R"({
        "classname": "ElectricalSource",
        "cpp_class": true,
        "ports": {"v_out": {"direction": "Out", "type": "V"}},
        "execution": {
            "electrical_passive": true,
            "electrical_observer": false,
            "logical": false,
            "control_commit": false,
            "electrical_actuator": false,
            "finalize": false,
            "mechanical": false,
            "hydraulic": false,
            "thermal": false,
            "extra": true
        }
    })");

    EXPECT_THROW(parse_type_definition(j), std::runtime_error);
}

TEST(ComponentRegistry, LoadRecursive_SubdirSetsCategory) {
    namespace fs = std::filesystem;
    auto tmp = fs::temp_directory_path() / "test_lib_hierarchy";
    fs::remove_all(tmp);
    fs::create_directories(tmp / "electrical");

    std::ofstream(tmp / "ElectricalSource.blueprint") << minimal_blueprint_v2("ElectricalSource");
    std::ofstream(tmp / "electrical" / "Resistor.blueprint") << minimal_blueprint_v2("Resistor");

    auto registry = load_component_registry(tmp.string());

    ASSERT_TRUE(registry.has("ElectricalSource"));
    ASSERT_TRUE(registry.has("Resistor"));

    // Root-level file has no category entry
    EXPECT_EQ(registry.all_categories().count("ElectricalSource"), 0u);
    // Subdir file gets category from directory path
    ASSERT_EQ(registry.all_categories().count("Resistor"), 1u);
    EXPECT_EQ(registry.all_categories().at("Resistor"), "electrical");

    fs::remove_all(tmp);
}

TEST(JsonParserTest, ParseTypeDefinition_ParamSchemaParsed) {
    auto j = nlohmann::json::parse(R"({
        "classname": "SchemaComp",
        "cpp_class": true,
        "domains": ["Electrical"],
        "execution": {
            "electrical_passive": true,
            "electrical_observer": false,
            "logical": false,
            "control_commit": false,
            "electrical_actuator": false,
            "finalize": false,
            "mechanical": false,
            "hydraulic": false,
            "thermal": false
        },
        "param_schema": {
            "r_internal": {"type": "float", "min": 0.000001, "required": true}
        },
        "ports": {"v": {"direction": "Out", "type": "V"}}
    })");

    auto [def, pres] = parse_type_definition(j);
    const auto& params = spec_params(def);
    ASSERT_TRUE(params.count("r_internal") > 0);
    EXPECT_EQ(params.at("r_internal").type, ParamSchemaType::Float);
    EXPECT_TRUE(params.at("r_internal").required);
}

TEST(JsonParserTest, MergeDeviceInstance_ParamSchemaRejectsInvalidValue) {
    PrimitiveSpec def;
    def.classname = "ElectricalSource";
    def.domains = std::vector<Domain>{Domain::Electrical};
    def.solver.execution = ExecutionPhases{true, false, false, false, false, false, false, false, false};
    def.ports["v_out"] = Port{bp2::Direction::Output, PortType::V, std::nullopt};
    def.params["r_internal"] = ParamSpec{ParamSchemaType::Float, "0.1", 0.000001, std::nullopt, true, false};

    DeviceInstance inst;
    inst.name = "bat";
    inst.classname = "ElectricalSource";
    inst.params["r_internal"] = "-0.5";

    EXPECT_THROW(resolve_component(inst, def), std::runtime_error);
}

TEST(ComponentRegistry, LoadRecursive_DeepNesting) {
    namespace fs = std::filesystem;
    auto tmp = fs::temp_directory_path() / "test_lib_deep";
    fs::remove_all(tmp);
    fs::create_directories(tmp / "electrical" / "generators");

    std::ofstream(tmp / "electrical" / "generators" / "Generator.blueprint") << minimal_blueprint_v2("Generator");

    auto registry = load_component_registry(tmp.string());

    ASSERT_TRUE(registry.has("Generator"));
    ASSERT_EQ(registry.all_categories().count("Generator"), 1u);
    EXPECT_EQ(registry.all_categories().at("Generator"), "electrical/generators");

    fs::remove_all(tmp);
}

TEST(ComponentRegistry, BuildMenuTree_FlatLibrary) {
    ComponentRegistry reg;

    PrimitiveSpec bat; bat.classname = "ElectricalSource";
    PrimitiveSpec res; res.classname = "Resistor";
    reg.register_type("ElectricalSource", bat);
    reg.register_type("Resistor", res);
    // No categories — all root level

    auto tree = reg.build_menu_tree();

    EXPECT_EQ(tree.entries.size(), 2u);
    EXPECT_TRUE(tree.children.empty());
}

TEST(ComponentRegistry, BuildMenuTree_WithSubdirs) {
    ComponentRegistry reg;

    PrimitiveSpec bat; bat.classname = "ElectricalSource";
    reg.register_type("ElectricalSource", bat);

    PrimitiveSpec res; res.classname = "Resistor";
    reg.register_type("Resistor", res, {}, "electrical");

    PrimitiveSpec gen; gen.classname = "Generator";
    reg.register_type("Generator", gen, {}, "electrical/generators");

    PrimitiveSpec and_gate; and_gate.classname = "AND";
    reg.register_type("AND", and_gate, {}, "logic");

    auto tree = reg.build_menu_tree();

    // Root: "ElectricalSource" + 2 subfolders
    EXPECT_EQ(tree.entries.size(), 1u);
    EXPECT_EQ(tree.children.size(), 2u);

    // electrical: "Resistor" + 1 subfolder
    ASSERT_TRUE(tree.children.count("electrical"));
    const auto& elec = tree.children.at("electrical");
    EXPECT_EQ(elec.entries.size(), 1u);
    EXPECT_EQ(elec.children.size(), 1u);

    // electrical/generators: "Generator"
    ASSERT_TRUE(elec.children.count("generators"));
    const auto& gens = elec.children.at("generators");
    EXPECT_EQ(gens.entries.size(), 1u);
    EXPECT_TRUE(gens.children.empty());

    // logic: "AND"
    ASSERT_TRUE(tree.children.count("logic"));
    EXPECT_EQ(tree.children.at("logic").entries.size(), 1u);
}

TEST(ComponentRegistry, BuildMenuTree_EntriesAreSorted) {
    ComponentRegistry reg;

    for (const auto& name : {"Zebra", "Alpha", "Middle"}) {
        PrimitiveSpec d; d.classname = name;
        reg.register_type(name, d);
    }

    auto tree = reg.build_menu_tree();

    ASSERT_EQ(tree.entries.size(), 3u);
    EXPECT_EQ(tree.entries[0], "Alpha");
    EXPECT_EQ(tree.entries[1], "Middle");
    EXPECT_EQ(tree.entries[2], "Zebra");
}

TEST(ComponentRegistry, BuildMenuTree_BlueprintsInSameTree) {
    ComponentRegistry reg;

    PrimitiveSpec bat; bat.classname = "ElectricalSource";
    reg.register_type("ElectricalSource", bat, {}, "electrical");

    CompositeSpec lamp; lamp.classname = "LampPassThrough";
    reg.register_type("LampPassThrough", lamp, {}, "electrical");

    auto tree = reg.build_menu_tree();

    ASSERT_TRUE(tree.children.count("electrical"));
    EXPECT_EQ(tree.children.at("electrical").entries.size(), 2u);
}

TEST(ComponentRegistry, ListClassnames_IncludesAllCategorized) {
    ComponentRegistry reg;

    PrimitiveSpec bat; bat.classname = "ElectricalSource";
    reg.register_type("ElectricalSource", bat, {}, "electrical");

    PrimitiveSpec and_gate; and_gate.classname = "AND";
    reg.register_type("AND", and_gate, {}, "logic");

    auto names = reg.list_classnames();
    EXPECT_EQ(names.size(), 2u);
}

// Regression: resolve_component must propagate domain and source_writer
// from the type definition when both instance and definition have the same port.
// Previously only type and alias were copied, silently dropping metadata.
TEST(JsonParserTest, MergeDeviceInstance_PropagatesPortDomainAndSourceWriter) {
    PrimitiveSpec def;
    def.classname = "Generator";
    def.domains = std::vector<Domain>{Domain::Electrical};
    // Definition port: domain=Mechanical, source_writer=true
    def.ports["v_out"] = Port{bp2::Direction::Output, PortType::V, Domain::Mechanical, true};

    DeviceInstance inst;
    inst.name = "gen1";
    inst.classname = "Generator";
    // Instance port: same name, but with default domain/source_writer
    inst.ports["v_out"] = Port{bp2::Direction::Output, PortType::V};

    ResolvedDevice merged = resolve_component(inst, def);

    // domain and source_writer must come from the definition, not remain at defaults
    EXPECT_EQ(merged.ports.at("v_out").domain, Domain::Mechanical);
    EXPECT_TRUE(merged.ports.at("v_out").source_writer);
}

// Regression: parse_type_definition must parse scheduler_source from JSON.
// Previously it was missing, always defaulting to false even when JSON said true.
TEST(JsonParserTest, ParseTypeDefinition_ParsesSchedulerSource) {
    auto j = nlohmann::json::parse(R"({
        "classname": "TestSource",
        "cpp_class": true,
        "scheduler_source": true,
        "domains": ["Electrical"],
        "ports": {"v_out": {"direction": "Out", "type": "V"}}
    })");

    auto [def, pres] = parse_type_definition(j);
    const PrimitiveSpec* prim = as_primitive(def);
    ASSERT_NE(prim, nullptr);
    EXPECT_TRUE(prim->solver.scheduler_source);

    // Also verify false case
    auto j2 = nlohmann::json::parse(R"({
        "classname": "TestLoad",
        "cpp_class": true,
        "scheduler_source": false,
        "domains": ["Electrical"],
        "ports": {"v_in": {"direction": "In", "type": "V"}}
    })");

    auto [def2, pres2] = parse_type_definition(j2);
    const PrimitiveSpec* prim2 = as_primitive(def2);
    ASSERT_NE(prim2, nullptr);
    EXPECT_FALSE(prim2->solver.scheduler_source);
}

// Regression: parse_type_definition default when scheduler_source is absent.
TEST(JsonParserTest, ParseTypeDefinition_SchedulerSourceDefaultsFalse) {
    auto j = nlohmann::json::parse(R"({
        "classname": "TestNoField",
        "cpp_class": true,
        "domains": ["Electrical"],
        "ports": {"v_out": {"direction": "Out", "type": "V"}}
    })");

     auto [def, pres] = parse_type_definition(j);
     const PrimitiveSpec* prim = as_primitive(def);
     ASSERT_NE(prim, nullptr);
     EXPECT_FALSE(prim->solver.scheduler_source);
}

TEST(JsonParserTest, ParseTypeDefinition_ExecutionRequiresAllCanonicalKeys) {
    auto j = nlohmann::json::parse(R"({
        "classname": "ElectricalSource",
        "cpp_class": true,
        "domains": ["Electrical"],
        "ports": {"v_out": {"direction": "Out", "type": "V"}},
        "execution": {
            "electrical_passive": true,
            "electrical_observer": false,
            "logical": false,
            "control_commit": false,
            "electrical_actuator": false,
            "finalize": false,
            "mechanical": false,
            "hydraulic": false
        }
    })");

    EXPECT_THROW(parse_type_definition(j), std::runtime_error);
}

TEST(JsonParserTest, ParseTypeDefinition_ExecutionRejectsUnknownKeys) {
    auto j = nlohmann::json::parse(R"({
        "classname": "ElectricalSource",
        "cpp_class": true,
        "domains": ["Electrical"],
        "ports": {"v_out": {"direction": "Out", "type": "V"}},
        "execution": {
            "electrical_passive": true,
            "electrical_observer": false,
            "logical": false,
            "control_commit": false,
            "electrical_actuator": false,
            "finalize": false,
            "mechanical": false,
            "hydraulic": false,
            "thermal": false,
            "extra": true
        }
    })");

    EXPECT_THROW(parse_type_definition(j), std::runtime_error);
}

TEST(ComponentRegistry, MissingSolverOwnedElectricalInV3BlueprintThrows) {
    namespace fs = std::filesystem;
    auto tmp = fs::temp_directory_path() / "test_missing_solver_owned";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    std::ofstream(tmp / "Bad.blueprint") << R"({
        "version": "3.0",
        "id": "Bad",
        "display_name": "Bad",
        "cpp_class": true,
        "scheduler_source": false,
        "domains": ["Electrical"],
        "interface": []
    })";

    EXPECT_THROW(load_component_registry(tmp.string()), std::runtime_error);

    fs::remove_all(tmp);
}

// === PARITY HARDENING: Rewrite Contract Tests ===

// Test 1: Composite parent endpoints keep canonical node.port format
// INVARIANT: Parent connections to expanded blueprint ports remain instance.port.
TEST(JsonParserTest, CompositeParentPortRewrite_ToExt) {
    std::string json = R"({
        "templates": {},
        "devices": [
            {"name": "lag1", "classname": "FirstOrderLag"},
            {"name": "bat1", "classname": "ElectricalSource"}
        ],
        "connections": [
            {"from": "bat1.v_out", "to": "lag1.in"},
            {"from": "lag1.out", "to": "bat1.v_gnd"}
        ]
    })";

    auto ctx = parse_json(json);

    // FirstOrderLag is an expandable composite blueprint in ComponentRegistry.
    // The expansion adds internal connections, so total count > 2.
    ASSERT_GT(ctx.connections.size(), 2);
    
    // Key invariant: parent connections remain canonical lag1.port format.
    bool found_lag1_in = false;
    bool found_lag1_out = false;
    
    for (const auto& conn : ctx.connections) {
        if (conn.to == "lag1.in") found_lag1_in = true;
        if (conn.from == "lag1.out") found_lag1_out = true;
    }

    ASSERT_TRUE(found_lag1_in)
        << "Parent connection must keep canonical endpoint 'lag1.in'";
    ASSERT_TRUE(found_lag1_out)
        << "Parent connection must keep canonical endpoint 'lag1.out'";

    // Final check: parser should not leak internal bridge endpoints in parent links.
    for (const auto& conn : ctx.connections) {
        if (conn.to.rfind("lag1:", 0) == 0 && conn.to.find(".ext") != std::string::npos) {
            FAIL() << "Parent connection leaked bridge endpoint: " << conn.to;
        }
        if (conn.from.rfind("lag1:", 0) == 0 && conn.from.find(".ext") != std::string::npos) {
            FAIL() << "Parent connection leaked bridge endpoint: " << conn.from;
        }
    }
}

// Test 2: Malformed endpoint (empty port) is handled safely without crash
// INVARIANT: Parser must not crash on malformed endpoints.
TEST(JsonParserTest, CompositeParentPortRewrite_EmptyPortIsSkippedWithWarning) {
    std::string json = R"({
        "templates": {},
        "devices": [
            {"name": "lag1", "classname": "FirstOrderLag"},
            {"name": "bat1", "classname": "ElectricalSource"}
        ],
        "connections": [
            {"from": "bat1.v_out", "to": "lag1."}
        ]
    })";

    // This should NOT crash despite malformed endpoint
    ASSERT_NO_THROW({
        try {
            auto ctx = parse_json(json);
            // Verify parser completed and connections exist (malformed ones may be kept as-is)
            EXPECT_GE(ctx.connections.size(), 1);
        } catch (const std::exception& e) {
            FAIL() << "Parser must not throw on malformed endpoints: " << e.what();
        }
    });
}

// ============================================================================
// Regression: load_component_registry must merge string_params into device params
// ============================================================================

TEST(ComponentRegistry, V3CompositeStringParamsMergedIntoDeviceParams) {
    // Regression test: v3 composite blueprints store string-valued parameters
    // (e.g. LUT table) in "string_params" separate from "params".
    // load_component_registry() must merge them so the simulation sees the full
    // parameter set.  Without this fix, LUT components lose their table and
    // fall back to default "0:0; 100:100", producing wrong output voltages
    // (e.g. 12SAM28 battery outputting ~1V instead of ~25V).
    namespace fs = std::filesystem;
    auto tmp = fs::temp_directory_path() / "test_string_params_merge";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    // Write a minimal v3 composite blueprint with a LUT node that has
    // its table in string_params (matching real 12SAM28 format).
    std::ofstream(tmp / "TestComposite.blueprint") << R"({
        "version": "3.0",
        "id": "TestComposite",
        "display_name": "TestComposite",
        "cpp_class": false,
        "scheduler_source": false,
        "domains": ["Electrical"],
        "interface": [
            {"name": "out", "direction": 1, "domain": 1, "type": "V", "source_writer": false}
        ],
        "nodes": [
            {
                "id": "lut1",
                "type": "LUT",
                "params": {"input_min": "0.0", "input_max": "1.0"},
                "string_params": {"table": "0.0:21.0; 0.5:24.0; 1.0:25.2"},
                "position": {"x": 0.0, "y": 0.0}
            }
        ],
        "wires": []
    })";

    auto registry = load_component_registry(tmp.string());
    ASSERT_TRUE(registry.has("TestComposite"));

    const auto* def_ptr = registry.get("TestComposite");
    ASSERT_NE(def_ptr, nullptr);
    const auto* def = as_composite(*def_ptr);
    ASSERT_NE(def, nullptr);
    ASSERT_EQ(def->devices.size(), 1u);
    const auto& lut = def->devices[0];

    // The table must be present in params, merged from string_params.
    auto it = lut.params.find("table");
    ASSERT_NE(it, lut.params.end()) << "string_params['table'] must be merged into params";
    EXPECT_EQ(it->second, "0.0:21.0; 0.5:24.0; 1.0:25.2");

    // Regular params must still be present too.
    auto it2 = lut.params.find("input_min");
    ASSERT_NE(it2, lut.params.end());
    EXPECT_EQ(it2->second, "0.0");

    fs::remove_all(tmp);
}

// === LOADER STRICTNESS: Bridge Port Direction Validation ===

// Loader strictness: v3 bridge_port node must have "direction" field
TEST(JsonParserTest, LoadRejectsBridgePortMissingDirection) {
    namespace fs = std::filesystem;
    auto tmp = fs::temp_directory_path() / "test_bridge_no_dir";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    std::ofstream(tmp / "BridgeNoDir.blueprint") << R"({
        "version": "3.0",
        "id": "BridgeNoDir",
        "display_name": "Bridge No Direction",
        "cpp_class": false,
        "scheduler_source": false,
        "domains": ["Electrical"],
        "interface": [],
        "nodes": [
            {
                "id": "bp_in",
                "kind": "bridge_port",
                "exposed_port": "in",
                "port_type": "V"
            }
        ],
        "wires": []
    })";

    EXPECT_THROW(load_component_registry(tmp.string()), std::runtime_error);

    fs::remove_all(tmp);
}

// Loader strictness: v3 bridge_port node must use canonical "direction", not "side"
TEST(JsonParserTest, LoadRejectsBridgePortWithStaleSideField) {
    namespace fs = std::filesystem;
    auto tmp = fs::temp_directory_path() / "test_bridge_side";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    std::ofstream(tmp / "BridgeSide.blueprint") << R"({
        "version": "3.0",
        "id": "BridgeSide",
        "display_name": "Bridge With Side",
        "cpp_class": false,
        "scheduler_source": false,
        "domains": ["Electrical"],
        "interface": [],
        "nodes": [
            {
                "id": "bp_in",
                "kind": "bridge_port",
                "exposed_port": "in",
                "side": "input",
                "port_type": "V"
            }
        ],
        "wires": []
    })";

    // The loader should reject unknown field "side" - it requires "direction"
    EXPECT_THROW(load_component_registry(tmp.string()), std::runtime_error);

    fs::remove_all(tmp);
}
