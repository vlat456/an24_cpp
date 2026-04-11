#include <gtest/gtest.h>
#include "json_parser/json_parser.h"
#include "core/solvers/jit/jit_solver.h"


/// Test that Editor can build a simple circuit with ComponentVariant
TEST(EditorComponentVariant, BuildSimpleBatteryResistorCircuit) {
    // Simple JSON with ElectricalSource + Resistor
    const char* json = R"({
        "devices": [
            {
                "name": "gnd",
                "classname": "RefNode",
                "params": {"value": "0.0"}
            },
            {
                "name": "bat1",
                "classname": "ElectricalSource",
                "priority": "high",
                "critical": true,
                "ports": {
                    "v_in": {"direction": "In", "type": "V"},
                    "v_out": {"direction": "Out", "type": "V"}
                },
                "params": {
                    "voltage": "24.0",
                    "resistance": "0.01"
                }
            },
            {
                "name": "load1",
                "classname": "Resistor",
                "ports": {
                    "v_in": {"direction": "In", "type": "V"},
                    "v_out": {"direction": "Out", "type": "V"}
                },
                "params": {
                    "conductance": "0.1"
                }
            }
        ],
        "connections": [
            {"from": "gnd.v", "to": "bat1.v_in"},
            {"from": "bat1.v_out", "to": "load1.v_in"},
            {"from": "load1.v_out", "to": "gnd.v"}
        ]
    })";

    // Parse JSON and build simulation
    JitBuildInput input = build_input_from_json(json);

    // Build using build_systems_dev directly
    auto build_result = build_systems_dev(input);

    // Check that devices map was populated
    EXPECT_EQ(build_result.devices.size(), 3);
    EXPECT_NE(build_result.devices.find("bat1"), build_result.devices.end());
    EXPECT_NE(build_result.devices.find("load1"), build_result.devices.end());

    // Check port mapping
    EXPECT_NE(build_result.port_to_signal.find("bat1.v_out"), build_result.port_to_signal.end());
    EXPECT_NE(build_result.port_to_signal.find("load1.v_in"), build_result.port_to_signal.end());
}

/// Test that multi-domain components work correctly
TEST(EditorComponentVariant, MultiDomainComponents) {
    // JSON with Electrical + Mechanical components
    const char* json = R"({
        "devices": [
            {
                "name": "gnd",
                "classname": "RefNode",
                "params": {"value": "0.0"}
            },
            {
                "name": "bat1",
                "classname": "ElectricalSource",
                "ports": {
                    "v_in": {"direction": "In", "type": "V"},
                    "v_out": {"direction": "Out", "type": "V"}
                }
            },
            {
                "name": "inertia1",
                "classname": "InertiaNode",
                "ports": {
                    "torque_in": {"direction": "In", "type": "Any"},
                    "rpm_out": {"direction": "Out", "type": "Any"}
                }
            }
        ],
        "connections": []
    })";

    auto ctx = parse_json(json);

    auto build_result = build_systems_dev(build_input_from_json(json));

    // Should have both components created
    EXPECT_EQ(build_result.devices.size(), 3);
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
                "classname": "ElectricalSource",
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

    auto build_result = build_systems_dev(build_input_from_json(json));

    // Ground should be marked as fixed signal
    EXPECT_FALSE(build_result.fixed_signals.empty());
}

/// Test that all 29 component types can be created
TEST(EditorComponentVariant, AllComponentTypes) {
        const char* component_types[] = {
            "ElectricalSource", "Bus", "Comparator",
            "ElectricHeater", "ElectricPump", "Generator", "Gyroscope",
            "HoldButton", "IndicatorLight", "InertiaNode", "Inverter",
            "LerpNode", "Radiator",
            "RefNode", "Relay", "Resistor", "SolenoidValve", "Splitter",
            "Switch", "TempSensor", "Transformer", "Voltmeter"
        };

    for (const char* type : component_types) {
        // Create simple JSON with one component
        std::string json = R"({
            "devices": [
                {
                    "name": "gnd",
                    "classname": "RefNode",
                    "params": {"value": "0.0"}
                },
                {
                    "name": "comp1",
                    "classname": ")" + std::string(type) + R"("
                }
            ],
            "connections": []
        })";

        if (std::string(type) == "RefNode") {
            json = R"({
                "devices": [
                    {
                        "name": "comp1",
                        "classname": "RefNode",
                        "params": {"value": "0.0"}
                    }
                ],
                "connections": []
            })";
        }

        auto ctx = parse_json(json.c_str());

        // Should not throw exception
        EXPECT_NO_THROW({
            auto build_result = build_systems_dev(build_input_from_json(json));
            const size_t expected_devices = (std::string(type) == "RefNode") ? 1u : 2u;
            EXPECT_EQ(build_result.devices.size(), expected_devices);
        }) << "Failed to create component: " << type;
    }
}

/// Test that ComponentVariant can be created via factory
TEST(EditorComponentVariant, FactoryCreatesCorrectVariant) {
    const char* json = R"({
        "devices": [
            {
                "name": "gnd",
                "classname": "RefNode",
                "params": {"value": "0.0"}
            },
            {
                "name": "bat1",
                "classname": "ElectricalSource",
                "ports": {
                    "v_in": {"direction": "In", "type": "V"},
                    "v_out": {"direction": "Out", "type": "V"}
                }
            }
        ],
        "connections": [
            {"from": "gnd.v", "to": "bat1.v_in"}
        ]
    })";

    auto ctx = parse_json(json);

    auto build_result = build_systems_dev(build_input_from_json(json));

    // Check that bat1 device was created
    EXPECT_EQ(build_result.devices.size(), 2);
    EXPECT_NE(build_result.devices.find("bat1"), build_result.devices.end());
}
