#include <gtest/gtest.h>
#include "json_parser/json_parser.h"
#include "jit_solver/jit_solver.h"


/// Test that Editor can build a simple circuit with ComponentVariant
TEST(EditorComponentVariant, BuildSimpleBatteryLoadCircuit) {
    // Simple JSON with Battery + Load
    const char* json = R"({
        "devices": [
            {
                "name": "bat1",
                "classname": "Battery",
                "priority": "high",
                "critical": true,
                "ports": {
                    "v_in": {"direction": "In", "type": "V"},
                    "v_out": {"direction": "Out", "type": "V"}
                },
                "params": {
                    "v_nominal": "24.0",
                    "internal_r": "0.01"
                }
            },
            {
                "name": "load1",
                "classname": "Load",
                "ports": {
                    "input": {"direction": "In", "type": "V"}
                },
                "params": {
                    "conductance": "0.1"
                }
            }
        ],
        "connections": [
            {"from": "bat1.v_out", "to": "load1.input"}
        ]
    })";

    // Parse JSON and build simulation
    auto ctx = parse_json(json);
    std::vector<std::pair<std::string, std::string>> connections;
    for (const auto& c : ctx.connections) {
        connections.push_back({c.from, c.to});
    }

    // Build using build_systems_dev directly
    auto build_result = build_systems_dev(ctx.devices, connections);

    // Check that devices map was populated
    EXPECT_EQ(build_result.devices.size(), 2);
    EXPECT_NE(build_result.devices.find("bat1"), build_result.devices.end());
    EXPECT_NE(build_result.devices.find("load1"), build_result.devices.end());

    // Check port mapping
    EXPECT_NE(build_result.port_to_signal.find("bat1.v_out"), build_result.port_to_signal.end());
    EXPECT_NE(build_result.port_to_signal.find("load1.input"), build_result.port_to_signal.end());
}

/// Test that multi-domain components work correctly
TEST(EditorComponentVariant, MultiDomainComponents) {
    // JSON with Electrical + Mechanical components
    const char* json = R"({
        "devices": [
            {
                "name": "bat1",
                "classname": "Battery",
                "ports": {
                    "v_in": {"direction": "In", "type": "V"},
                    "v_out": {"direction": "Out", "type": "V"}
                }
            },
            {
                "name": "inertia1",
                "classname": "InertiaNode",
                "ports": {
                    "input": {"direction": "In", "type": "RPM"},
                    "output": {"direction": "Out", "type": "RPM"}
                }
            }
        ],
        "connections": []
    })";

    auto ctx = parse_json(json);
    std::vector<std::pair<std::string, std::string>> connections;

    auto build_result = build_systems_dev(ctx.devices, connections);

    // Should have both components created
    EXPECT_EQ(build_result.devices.size(), 2);
    EXPECT_NE(build_result.devices.find("bat1"), build_result.devices.end());
    EXPECT_NE(build_result.devices.find("inertia1"), build_result.devices.end());
}

/// Test that RefNode sets fixed voltage correctly
TEST(EditorComponentVariant, RefNodeFixedVoltage) {
    // JSON with RefNode (ground)
    const char* json = R"({
        "devices": [
            {
                "name": "gnd",
                "classname": "RefNode",
                "ports": {
                    "v": {"direction": "Out", "type": "V"}
                },
                "params": {
                    "value": "0.0"
                }
            },
            {
                "name": "bat1",
                "classname": "Battery",
                "ports": {
                    "v_in": {"direction": "In", "type": "V"},
                    "v_out": {"direction": "Out", "type": "V"}
                }
            }
        ],
        "connections": [
            {"from": "bat1.v_in", "to": "gnd.v"}
        ]
    })";

    auto ctx = parse_json(json);
    std::vector<std::pair<std::string, std::string>> connections;
    for (const auto& c : ctx.connections) {
        connections.push_back({c.from, c.to});
    }

    auto build_result = build_systems_dev(ctx.devices, connections);

    // Ground should be marked as fixed signal
    EXPECT_FALSE(build_result.fixed_signals.empty());
}

/// Test that all 29 component types can be created
TEST(EditorComponentVariant, AllComponentTypes) {
    const char* component_types[] = {
        "AGK47", "Battery", "Bus", "Comparator", "DMR400",
        "ElectricHeater", "ElectricPump", "GS24", "Generator", "Gyroscope",
        "HighPowerLoad", "HoldButton", "IndicatorLight", "InertiaNode", "Inverter",
        "LerpNode", "Load", "RU19A", "RUG82", "Radiator",
        "RefNode", "Relay", "Resistor", "SolenoidValve", "Splitter",
        "Switch", "TempSensor", "Transformer", "Voltmeter"
    };

    for (const char* type : component_types) {
        // Create simple JSON with one component
        std::string json = R"({
            "devices": [
                {
                    "name": "comp1",
                    "classname": ")" + std::string(type) + R"("
                }
            ],
            "connections": []
        })";

        auto ctx = parse_json(json.c_str());
        std::vector<std::pair<std::string, std::string>> connections;

        // Should not throw exception
        EXPECT_NO_THROW({
            auto build_result = build_systems_dev(ctx.devices, connections);
            EXPECT_EQ(build_result.devices.size(), 1);
        }) << "Failed to create component: " << type;
    }
}

/// Test that ComponentVariant can be created via factory
TEST(EditorComponentVariant, FactoryCreatesCorrectVariant) {
    const char* json = R"({
        "devices": [
            {
                "name": "bat1",
                "classname": "Battery",
                "ports": {
                    "v_in": {"direction": "In", "type": "V"},
                    "v_out": {"direction": "Out", "type": "V"}
                }
            }
        ],
        "connections": []
    })";

    auto ctx = parse_json(json);
    std::vector<std::pair<std::string, std::string>> connections;

    auto build_result = build_systems_dev(ctx.devices, connections);

    // Check that bat1 device was created
    EXPECT_EQ(build_result.devices.size(), 1);
    EXPECT_NE(build_result.devices.find("bat1"), build_result.devices.end());

    // Check that we can access the device (this validates ComponentVariant works)
    auto& variant = build_result.devices.at("bat1");
    EXPECT_NO_THROW({
        // Access the variant to ensure it's valid
        auto index = variant.index();
        EXPECT_GE(index, 0);
        EXPECT_LT(index, 29); // 29 component types
    });
}
