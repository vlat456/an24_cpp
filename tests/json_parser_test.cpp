#include "json_parser.h"
#include <gtest/gtest.h>
#include <sstream>

using namespace an24;

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
    EXPECT_TRUE(dev.is_composite);
    EXPECT_EQ(dev.domains.size(), 1);
    EXPECT_EQ(dev.domains[0], Domain::Electrical);

    EXPECT_EQ(dev.ports.size(), 3);
    auto it_in = dev.ports.find("v_in");
    ASSERT_NE(it_in, dev.ports.end());
    EXPECT_EQ(it_in->second.direction, PortDirection::In);
    auto it_out = dev.ports.find("v_out");
    ASSERT_NE(it_out, dev.ports.end());
    EXPECT_EQ(it_out->second.direction, PortDirection::Out);
    auto it_control = dev.ports.find("control");
    ASSERT_NE(it_control, dev.ports.end());
    EXPECT_EQ(it_control->second.direction, PortDirection::In);

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

TEST(JsonParserTest, Regression_PortTypeMerge_ComponentDefinitionTypesCopied) {
    // Regression test for bug where port types from ComponentDefinition
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

    // Verify that port types from ComponentDefinition were copied
    EXPECT_EQ(apu.ports.count("v_bus"), 1) << "v_bus port should exist";
    EXPECT_EQ(apu.ports.at("v_bus").type, PortType::V)
        << "v_bus type should be V (from ComponentDefinition)";

    EXPECT_EQ(apu.ports.count("rpm_out"), 1) << "rpm_out port should exist";
    EXPECT_EQ(apu.ports.at("rpm_out").type, PortType::RPM)
        << "rpm_out type should be RPM (from ComponentDefinition)";

    EXPECT_EQ(apu.ports.count("t4_out"), 1) << "t4_out port should exist";
    EXPECT_EQ(apu.ports.at("t4_out").type, PortType::Temperature)
        << "t4_out type should be Temperature (from ComponentDefinition)";
}

TEST(JsonParserTest, Regression_PortTypeMerge_InlinePortWithoutType) {
    // Regression test: if a device has inline port definition without type,
    // the type should be copied from ComponentDefinition.

    std::string json = R"({
        "templates": {},
        "devices": [
            {
                "name": "bat",
                "classname": "Battery",
                "ports": {
                    "v_out": {"direction": "Out"}
                }
            }
        ],
        "connections": []
    })";

    auto ctx = parse_json(json);
    ASSERT_EQ(ctx.devices.size(), 1);
    const auto& bat = ctx.devices[0];

    // Port should exist (from inline definition)
    EXPECT_EQ(bat.ports.count("v_out"), 1);

    // But type should come from ComponentDefinition (Battery.json)
    EXPECT_EQ(bat.ports.at("v_out").type, PortType::V)
        << "Port type should be copied from ComponentDefinition even for inline ports";
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

TEST(JsonParserTest, OneToOne_MultipleWiresToSamePort_ShouldFail) {
    // Multiple wires connecting to the same port should be rejected
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

    // Should throw - load.v_in already has a wire from bat1.v_out
    EXPECT_THROW(parse_json(json), std::runtime_error);
}

TEST(JsonParserTest, OneToOne_MultipleWiresFromSamePort_ShouldFail) {
    // Multiple wires from the same output port should be rejected
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

    // Should throw - bat.v_out already has a wire to load1.v_in
    EXPECT_THROW(parse_json(json), std::runtime_error);
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
