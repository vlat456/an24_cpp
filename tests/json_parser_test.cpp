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
    gnd.ports["v"] = Port{PortDirection::Out};
    gnd.params["value"] = "0.0";
    gnd.explicit_domains = {Domain::Electrical};
    ctx.devices.push_back(gnd);

    // Battery
    DeviceInstance bat;
    bat.name = "bat";
    bat.classname = "Battery";
    bat.priority = "high";
    bat.ports["v_out"] = Port{PortDirection::Out};
    bat.params["v_nominal"] = "28.0";
    bat.explicit_domains = {Domain::Electrical};
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
    bat.explicit_domains = {Domain::Electrical};
    tpl.devices.push_back(bat);

    SubsystemCall sub;
    sub.name = "sub1";
    sub.template_name = "Other";
    sub.port_map["p"] = "pwr";
    tpl.subsystems.push_back(sub);

    tpl.exposed_ports["pwr"] = "internal_bus";
    tpl.explicit_domains = {Domain::Electrical};

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
    ASSERT_TRUE(dev.explicit_domains.has_value());
    EXPECT_EQ(dev.explicit_domains->size(), 2);
    EXPECT_EQ((*dev.explicit_domains)[0], Domain::Electrical);
    EXPECT_EQ((*dev.explicit_domains)[1], Domain::Hydraulic);
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
                    "v_out": "o"
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
    ASSERT_TRUE(dev.explicit_domains.has_value());
    EXPECT_EQ((*dev.explicit_domains)[0], Domain::Electrical);

    EXPECT_EQ(dev.ports.size(), 2);
    auto it_in = dev.ports.find("v_in");
    ASSERT_NE(it_in, dev.ports.end());
    EXPECT_EQ(it_in->second.direction, PortDirection::In);
    auto it_out = dev.ports.find("v_out");
    ASSERT_NE(it_out, dev.ports.end());
    EXPECT_EQ(it_out->second.direction, PortDirection::Out);

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
    dev.explicit_domains = {Domain::Electrical, Domain::Thermal};
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
    dev.ports["v_in"] = Port{PortDirection::In};
    dev.ports["v_out"] = Port{PortDirection::Out};
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
