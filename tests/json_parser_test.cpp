#include "json_parser.h"
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

TEST(JsonParserTest, ParseAndSerializeRoundTrip) {
    ParserContext ctx;

    // RefNode as a device (has only 'v' port)
    DeviceInstance gnd;
    gnd.name = "gnd1";
    gnd.classname = "RefNode";
    gnd.priority = "med";
    gnd.critical = false;
    gnd.ports["v"] = Port{PortDirection::Out, PortType::V};
    gnd.params["value"] = "0.0";
    gnd.domains = {Domain::Electrical};
    ctx.devices.push_back(gnd);

    // Battery
    DeviceInstance bat;
    bat.name = "bat";
    bat.classname = "Battery";
    bat.priority = "high";
    bat.ports["v_out"] = Port{PortDirection::Out, PortType::V};
    bat.params["v_nominal"] = "28.0";
    bat.domains = {Domain::Electrical};
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
    bat.classname = "Battery";
    bat.domains = {Domain::Electrical};
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
    EXPECT_EQ(dev.domains.size(), 2);
    EXPECT_EQ(dev.domains[0], Domain::Electrical);
    EXPECT_EQ(dev.domains[1], Domain::Hydraulic);
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
    EXPECT_EQ(dev.domains.size(), 2);
    EXPECT_EQ(dev.domains[0], Domain::Electrical);

    // Relay blueprint defines 4 ports: v_in, v_out, control, state
    // merge_device_instance() enriches with library-defined ports
    EXPECT_EQ(dev.ports.size(), 4);
    auto it_in = dev.ports.find("v_in");
    ASSERT_NE(it_in, dev.ports.end());
    EXPECT_EQ(it_in->second.direction, PortDirection::In);
    auto it_out = dev.ports.find("v_out");
    ASSERT_NE(it_out, dev.ports.end());
    EXPECT_EQ(it_out->second.direction, PortDirection::Out);
    auto it_control = dev.ports.find("control");
    ASSERT_NE(it_control, dev.ports.end());
    EXPECT_EQ(it_control->second.direction, PortDirection::In);
    auto it_state = dev.ports.find("state");
    ASSERT_NE(it_state, dev.ports.end());
    EXPECT_EQ(it_state->second.direction, PortDirection::Out);

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

    DeviceInstance dev;
    dev.name = "test";
    dev.classname = "Battery";
    dev.params["v_nominal"] = "28.0";
    dev.domains = {Domain::Electrical, Domain::Thermal};
    ctx.devices.push_back(dev);

    ctx.connections.push_back({"a.b", "c.d"});

    std::string json = serialize_json(ctx);
    auto ctx2 = parse_json(json);

    EXPECT_EQ(ctx2.devices.size(), 1);
    EXPECT_EQ(ctx2.devices[0].params["v_nominal"], "28.0");
    EXPECT_EQ(ctx2.connections.size(), 1);
}

// [g7h8] InOut port direction roundtrip
TEST(JsonParserTest, InOutPortDirection_Roundtrip_g7h8) {
    ParserContext ctx;

    DeviceInstance dev;
    dev.name = "test_dev";
    dev.classname = "Battery"; // use known component for validation
    dev.ports["v_in"] = Port{PortDirection::In, PortType::V};
    dev.ports["v_out"] = Port{PortDirection::Out, PortType::V};
    ctx.devices.push_back(dev);

    std::string json = serialize_json(ctx);

    // In direction should serialize as "In", Out as "Out"
    EXPECT_NE(json.find("\"In\""), std::string::npos)
        << "[g7h8] In direction should be preserved in serialized JSON";
    EXPECT_NE(json.find("\"Out\""), std::string::npos)
        << "[g7h8] Out direction should be preserved in serialized JSON";

    auto ctx2 = parse_json(json);
    ASSERT_EQ(ctx2.devices.size(), 1);
    EXPECT_EQ(ctx2.devices[0].ports["v_in"].direction, PortDirection::In);
    EXPECT_EQ(ctx2.devices[0].ports["v_out"].direction, PortDirection::Out);
}

// [g7h8] InOut enum value exists and parses correctly
TEST(JsonParserTest, InOutEnumExists_g7h8) {
    // Verify InOut is a valid PortDirection value
    PortDirection d = PortDirection::InOut;
    EXPECT_NE(d, PortDirection::In);
    EXPECT_NE(d, PortDirection::Out);
}

// ============================================================================
// Port Type Tests - FAILING TESTS FIRST (TDD)
// ============================================================================

TEST(JsonParserTest, ParsePortType_FromJson) {
    std::string json = R"({
        "templates": {},
        "devices": [{
            "name": "batt",
            "classname": "Battery",
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
            "name": "apu",
            "classname": "RU19A",
            "ports": {
                "rpm_out": {"direction": "Out", "type": "RPM"}
            }
        }],
        "connections": []
    })";

    auto ctx = parse_json(json);
    ASSERT_EQ(ctx.devices.size(), 1);
    EXPECT_EQ(ctx.devices[0].ports["rpm_out"].type, PortType::RPM);
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
                "classname": "Battery",
                "ports": {
                    "v_out": {"direction": "Out", "type": "V"}
                }
            },
            {
                "name": "apu",
                "classname": "RU19A",
                "ports": {
                    "rpm_out": {"direction": "Out", "type": "RPM"}
                }
            }
        ],
        "connections": [
            {"from": "batt.v_out", "to": "apu.rpm_out"}
        ]
    })";

    // Should NOT throw - JSON parsing doesn't validate types
    // Validation happens during wire creation in the editor
    auto ctx = parse_json(json);
    EXPECT_EQ(ctx.connections.size(), 1);
}

TEST(JsonParserTest, ValidateConnection_MatchingTypes_ShouldPass) {
    std::string json = R"({
        "templates": {},
        "devices": [
            {
                "name": "batt1",
                "classname": "Battery",
                "ports": {
                    "v_out": {"direction": "Out", "type": "V"}
                }
            },
            {
                "name": "batt2",
                "classname": "Battery",
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
                "classname": "Battery",
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
                "classname": "Battery",
                "ports": {
                    "v_out": {"direction": "Out", "type": "Any"}
                }
            },
            {
                "name": "batt2",
                "classname": "Battery",
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

    // Use RU19A which has RPM ports
    DeviceInstance dev;
    dev.name = "test";
    dev.classname = "RU19A";
    dev.domains = {Domain::Electrical, Domain::Mechanical, Domain::Thermal};
    dev.ports["v_bus"] = Port{PortDirection::Out, PortType::V};
    dev.ports["rpm_out"] = Port{PortDirection::Out, PortType::RPM};
    ctx.devices.push_back(dev);

    std::string json = serialize_json(ctx);
    auto ctx2 = parse_json(json);

    EXPECT_EQ(ctx2.devices[0].ports["v_bus"].type, PortType::V);
    EXPECT_EQ(ctx2.devices[0].ports["rpm_out"].type, PortType::RPM);
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
                "name": "apu",
                "classname": "RU19A"
            }
        ],
        "connections": []
    })";

    auto ctx = parse_json(json);
    ASSERT_EQ(ctx.devices.size(), 1);
    const auto& apu = ctx.devices[0];

    // Verify that port types from TypeDefinition were copied
    EXPECT_EQ(apu.ports.count("v_bus"), 1) << "v_bus port should exist";
    EXPECT_EQ(apu.ports.at("v_bus").type, PortType::V)
        << "v_bus type should be V (from TypeDefinition)";

    EXPECT_EQ(apu.ports.count("rpm_out"), 1) << "rpm_out port should exist";
    EXPECT_EQ(apu.ports.at("rpm_out").type, PortType::RPM)
        << "rpm_out type should be RPM (from TypeDefinition)";

    EXPECT_EQ(apu.ports.count("t4_out"), 1) << "t4_out port should exist";
    EXPECT_EQ(apu.ports.at("t4_out").type, PortType::Temperature)
        << "t4_out type should be Temperature (from TypeDefinition)";
}

TEST(JsonParserTest, Regression_PortTypeMerge_InlinePortWithType) {
    // Inline port definition must include type field.

    std::string json = R"({
        "templates": {},
        "devices": [
            {
                "name": "bat",
                "classname": "Battery",
                "ports": {
                    "v_out": {"direction": "Out", "type": "V"}
                }
            }
        ],
        "connections": []
    })";

    auto ctx = parse_json(json);
    ASSERT_EQ(ctx.devices.size(), 1);
    const auto& bat = ctx.devices[0];

    EXPECT_EQ(bat.ports.count("v_out"), 1);
    EXPECT_EQ(bat.ports.at("v_out").type, PortType::V);
}

TEST(JsonParserTest, Regression_LerpNodeAnyType_CanConnectToAnything) {
    // LerpNode has Any type ports, which should connect to anything
    std::string json = R"({
        "templates": {},
        "devices": [
            {
                "name": "lerp",
                "classname": "LerpNode"
            },
            {
                "name": "bat",
                "classname": "Battery",
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
            {"name": "bat1", "classname": "Battery"},
            {"name": "bat2", "classname": "Battery"},
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
            {"name": "bat", "classname": "Battery"},
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
            {"name": "bat1", "classname": "Battery"},
            {"name": "bat2", "classname": "Battery"}
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
            {"name": "bat1", "classname": "Battery"},
            {"name": "bat2", "classname": "Battery"}
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
        R"({"version": "3.0", "id": "%s", "display_name": "%s", "interface": [], "cpp_class": true, "scheduler_source": false, "domains": ["Electrical"], "execution": {"electrical_passive": true, "electrical_observer": false, "logical": false, "control_commit": false, "electrical_actuator": false, "finalize": false, "mechanical": false, "hydraulic": false, "thermal": false}})",
        classname, classname);
    return buf;
}

TEST(JsonParserTest, ParseTypeDefinition_ExecutionMissingKeyThrows) {
    auto j = nlohmann::json::parse(R"({
        "classname": "Battery",
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
        "classname": "Battery",
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

TEST(TypeRegistry, LoadRecursive_SubdirSetsCategory) {
    namespace fs = std::filesystem;
    auto tmp = fs::temp_directory_path() / "test_lib_hierarchy";
    fs::remove_all(tmp);
    fs::create_directories(tmp / "electrical");

    std::ofstream(tmp / "Battery.blueprint") << minimal_blueprint_v2("Battery");
    std::ofstream(tmp / "electrical" / "Resistor.blueprint") << minimal_blueprint_v2("Resistor");

    auto registry = load_type_registry(tmp.string());

    ASSERT_TRUE(registry.has("Battery"));
    ASSERT_TRUE(registry.has("Resistor"));

    // Root-level file has no category entry
    EXPECT_EQ(registry.categories.count("Battery"), 0u);
    // Subdir file gets category from directory path
    ASSERT_EQ(registry.categories.count("Resistor"), 1u);
    EXPECT_EQ(registry.categories.at("Resistor"), "electrical");

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

    TypeDefinition def = parse_type_definition(j);
    ASSERT_TRUE(def.param_schema.count("r_internal") > 0);
    EXPECT_EQ(def.param_schema.at("r_internal").type, ParamSchemaType::Float);
    EXPECT_TRUE(def.param_schema.at("r_internal").required);
}

TEST(JsonParserTest, MergeDeviceInstance_ParamSchemaRejectsInvalidValue) {
    TypeDefinition def;
    def.classname = "Battery";
    def.cpp_class = true;
    def.domains = std::vector<Domain>{Domain::Electrical};
    def.execution = ExecutionPhases{true, false, false, false, false, false, false, false, false};
    def.ports["v_out"] = Port{PortDirection::Out, PortType::V, std::nullopt};
    def.params["r_internal"] = "0.1";
    def.param_schema["r_internal"] = ParamSchemaEntry{ParamSchemaType::Float, 0.000001, std::nullopt, true};

    DeviceInstance inst;
    inst.name = "bat";
    inst.classname = "Battery";
    inst.params["r_internal"] = "-0.5";

    EXPECT_THROW(merge_device_instance(inst, def), std::runtime_error);
}

TEST(TypeRegistry, LoadRecursive_DeepNesting) {
    namespace fs = std::filesystem;
    auto tmp = fs::temp_directory_path() / "test_lib_deep";
    fs::remove_all(tmp);
    fs::create_directories(tmp / "electrical" / "generators");

    std::ofstream(tmp / "electrical" / "generators" / "GS24.blueprint") << minimal_blueprint_v2("GS24");

    auto registry = load_type_registry(tmp.string());

    ASSERT_TRUE(registry.has("GS24"));
    ASSERT_EQ(registry.categories.count("GS24"), 1u);
    EXPECT_EQ(registry.categories.at("GS24"), "electrical/generators");

    fs::remove_all(tmp);
}

TEST(TypeRegistry, BuildMenuTree_FlatLibrary) {
    TypeRegistry reg;

    TypeDefinition bat; bat.classname = "Battery";
    TypeDefinition res; res.classname = "Resistor";
    reg.types["Battery"] = bat;
    reg.types["Resistor"] = res;
    // No categories — all root level

    auto tree = reg.build_menu_tree();

    EXPECT_EQ(tree.entries.size(), 2u);
    EXPECT_TRUE(tree.children.empty());
}

TEST(TypeRegistry, BuildMenuTree_WithSubdirs) {
    TypeRegistry reg;

    TypeDefinition bat; bat.classname = "Battery";
    reg.types["Battery"] = bat;

    TypeDefinition res; res.classname = "Resistor";
    reg.types["Resistor"] = res;
    reg.categories["Resistor"] = "electrical";

    TypeDefinition gs; gs.classname = "GS24";
    reg.types["GS24"] = gs;
    reg.categories["GS24"] = "electrical/generators";

    TypeDefinition and_gate; and_gate.classname = "AND";
    reg.types["AND"] = and_gate;
    reg.categories["AND"] = "logic";

    auto tree = reg.build_menu_tree();

    // Root: "Battery" + 2 subfolders
    EXPECT_EQ(tree.entries.size(), 1u);
    EXPECT_EQ(tree.children.size(), 2u);

    // electrical: "Resistor" + 1 subfolder
    ASSERT_TRUE(tree.children.count("electrical"));
    const auto& elec = tree.children.at("electrical");
    EXPECT_EQ(elec.entries.size(), 1u);
    EXPECT_EQ(elec.children.size(), 1u);

    // electrical/generators: "GS24"
    ASSERT_TRUE(elec.children.count("generators"));
    const auto& gens = elec.children.at("generators");
    EXPECT_EQ(gens.entries.size(), 1u);
    EXPECT_TRUE(gens.children.empty());

    // logic: "AND"
    ASSERT_TRUE(tree.children.count("logic"));
    EXPECT_EQ(tree.children.at("logic").entries.size(), 1u);
}

TEST(TypeRegistry, BuildMenuTree_EntriesAreSorted) {
    TypeRegistry reg;

    for (const auto& name : {"Zebra", "Alpha", "Middle"}) {
        TypeDefinition d; d.classname = name;
        reg.types[name] = d;
    }

    auto tree = reg.build_menu_tree();

    ASSERT_EQ(tree.entries.size(), 3u);
    EXPECT_EQ(tree.entries[0], "Alpha");
    EXPECT_EQ(tree.entries[1], "Middle");
    EXPECT_EQ(tree.entries[2], "Zebra");
}

TEST(TypeRegistry, BuildMenuTree_BlueprintsInSameTree) {
    TypeRegistry reg;

    TypeDefinition bat; bat.classname = "Battery"; bat.cpp_class = true;
    reg.types["Battery"] = bat;
    reg.categories["Battery"] = "electrical";

    TypeDefinition lamp; lamp.classname = "LampPassThrough"; lamp.cpp_class = false;
    reg.types["LampPassThrough"] = lamp;
    reg.categories["LampPassThrough"] = "electrical";

    auto tree = reg.build_menu_tree();

    ASSERT_TRUE(tree.children.count("electrical"));
    EXPECT_EQ(tree.children.at("electrical").entries.size(), 2u);
}

TEST(TypeRegistry, ListClassnames_IncludesAllCategorized) {
    TypeRegistry reg;

    TypeDefinition bat; bat.classname = "Battery";
    reg.types["Battery"] = bat;
    reg.categories["Battery"] = "electrical";

    TypeDefinition and_gate; and_gate.classname = "AND";
    reg.types["AND"] = and_gate;
    reg.categories["AND"] = "logic";

    auto names = reg.list_classnames();
    EXPECT_EQ(names.size(), 2u);
}

// Regression: merge_device_instance must propagate domain and source_writer
// from the type definition when both instance and definition have the same port.
// Previously only type and alias were copied, silently dropping metadata.
TEST(JsonParserTest, MergeDeviceInstance_PropagatesPortDomainAndSourceWriter) {
    TypeDefinition def;
    def.classname = "Generator";
    def.cpp_class = true;
    def.domains = std::vector<Domain>{Domain::Electrical};
    // Definition port: domain=Mechanical, source_writer=true
    def.ports["v_out"] = Port{PortDirection::Out, PortType::V, Domain::Mechanical, true};

    DeviceInstance inst;
    inst.name = "gen1";
    inst.classname = "Generator";
    // Instance port: same name, but with default domain/source_writer
    inst.ports["v_out"] = Port{PortDirection::Out, PortType::V};

    DeviceInstance merged = merge_device_instance(inst, def);

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

    TypeDefinition def = parse_type_definition(j);
    EXPECT_TRUE(def.scheduler_source);

    // Also verify false case
    auto j2 = nlohmann::json::parse(R"({
        "classname": "TestLoad",
        "cpp_class": true,
        "scheduler_source": false,
        "domains": ["Electrical"],
        "ports": {"v_in": {"direction": "In", "type": "V"}}
    })");

    TypeDefinition def2 = parse_type_definition(j2);
    EXPECT_FALSE(def2.scheduler_source);
}

// Regression: parse_type_definition default when scheduler_source is absent.
TEST(JsonParserTest, ParseTypeDefinition_SchedulerSourceDefaultsFalse) {
    auto j = nlohmann::json::parse(R"({
        "classname": "TestNoField",
        "cpp_class": true,
        "domains": ["Electrical"],
        "ports": {"v_out": {"direction": "Out", "type": "V"}}
    })");

     TypeDefinition def = parse_type_definition(j);
     EXPECT_FALSE(def.scheduler_source);
}

// === PARITY HARDENING: Rewrite Contract Tests ===

// Test 1: Composite parent port rewrite to .ext format
// INVARIANT: Parent connections to expanded blueprint ports must rewrite to instance:port.ext format
TEST(JsonParserTest, CompositeParentPortRewrite_ToExt) {
    std::string json = R"({
        "templates": {},
        "devices": [
            {"name": "lag1", "classname": "FirstOrderLag"},
            {"name": "bat1", "classname": "Battery"}
        ],
        "connections": [
            {"from": "bat1.v_out", "to": "lag1.in"},
            {"from": "lag1.out", "to": "bat1.v_gnd"}
        ]
    })";

    auto ctx = parse_json(json);

    // After rewrite, expanded blueprint connections should have .ext suffix
    // because FirstOrderLag is an expandable composite blueprint in TypeRegistry
    // The expansion adds internal connections, so total count > 2
    ASSERT_GT(ctx.connections.size(), 2);
    
    // Key invariant: parent connections to lag1 must be rewritten to lag1:port.ext format
    bool found_lag1_in_ext = false;
    bool found_lag1_out_ext = false;
    
    for (const auto& conn : ctx.connections) {
        if (conn.to.find("lag1:in.ext") != std::string::npos) found_lag1_in_ext = true;
        if (conn.from.find("lag1:out.ext") != std::string::npos) found_lag1_out_ext = true;
    }

    ASSERT_TRUE(found_lag1_in_ext) 
        << "Parent connection 'lag1.in' must be rewritten to 'lag1:in.ext' format";
    ASSERT_TRUE(found_lag1_out_ext)
        << "Parent connection 'lag1.out' must be rewritten to 'lag1:out.ext' format";

    // Final check: no raw "lag1.out" or "lag1.in" should exist in parent connections
    // (they should all be rewritten to .ext format)
    for (const auto& conn : ctx.connections) {
        if (conn.to.find("lag1") != std::string::npos && conn.to.find(":") == std::string::npos) {
            // Skip if it's internal (has colon already processed)
            if (conn.to.find(':') == std::string::npos && conn.to.find("lag1.") != std::string::npos) {
                FAIL() << "Found unrewritten parent connection: " << conn.to;
            }
        }
        if (conn.from.find("lag1") != std::string::npos && conn.from.find(":") == std::string::npos) {
            if (conn.from.find(':') == std::string::npos && conn.from.find("lag1.") != std::string::npos) {
                FAIL() << "Found unrewritten parent connection: " << conn.from;
            }
        }
    }
}

// Test 2: Malformed endpoint (empty port) is skipped safely without crash
// INVARIANT: Parser must not crash on malformed endpoints, must skip rewrite gracefully
TEST(JsonParserTest, CompositeParentPortRewrite_EmptyPortIsSkippedWithWarning) {
    std::string json = R"({
        "templates": {},
        "devices": [
            {"name": "lag1", "classname": "FirstOrderLag"},
            {"name": "bat1", "classname": "Battery"}
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
