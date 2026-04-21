#include <gtest/gtest.h>

#include "core/solvers/aot/codegen_internal.h"
#include "core/solvers/jit/jit_solver.h"
#include "core/solvers/jit/components/port_registry.h"

#include "io/json/component_registry_json_loader.h"
#include "core/registry/component_resolution.h"
#include "jit_build_input_test_helper.h"

namespace {

const ComponentRegistry& shared_registry() {
    static const ComponentRegistry registry = load_component_registry("library/");
    return registry;
}

BridgePortDefinition make_bridge(std::string name, bp2::BridgeDirection direction) {
    BridgePortDefinition bridge;
    bridge.id = std::move(name);
    bridge.exposed_port = bridge.id;
    bridge.direction = direction;
    bridge.type = PortType::Any;
    return bridge;
}

DeviceInstance make_resistor_device(std::string name) {
    DeviceInstance dev;
    dev.name = std::move(name);
    dev.classname = "Resistor";
    dev.params["conductance"] = "1.0";
    return dev;
}

} // namespace

TEST(BridgeLowering, BuildSkipsBridgeRuntimeComponents) {
    std::vector<DeviceInstance> devices;
    devices.push_back(make_resistor_device("load"));
    std::vector<BridgePortDefinition> bridges = {
        make_bridge("vin", bp2::BridgeDirection::Input),
        make_bridge("vout", bp2::BridgeDirection::Output),
    };

    std::vector<Connection> connections = {
        {"vin.port", "load.v_in", {}},
        {"load.v_out", "vout.port", {}},
    };

    BuildResult result = build_systems_dev(make_jit_input_from_composite(devices, bridges, connections));

    EXPECT_EQ(result.devices.count("vin"), 0u);
    EXPECT_EQ(result.devices.count("vout"), 0u);
    EXPECT_EQ(result.devices.count("load"), 1u);
}

TEST(BridgeLowering, BridgeSignalsStillUnifiedForAliasContract) {
    std::vector<DeviceInstance> devices;
    devices.push_back(make_resistor_device("load"));
    std::vector<BridgePortDefinition> bridges = {
        make_bridge("vin", bp2::BridgeDirection::Input),
    };

    std::vector<Connection> connections = {
        {"vin.port", "load.v_in", {}},
    };

    BuildResult result = build_systems_dev(make_jit_input_from_composite(devices, bridges, connections));

    ASSERT_TRUE(result.port_to_signal.count("vin.ext") > 0);
    ASSERT_TRUE(result.port_to_signal.count("vin.port") > 0);
    ASSERT_TRUE(result.port_to_signal.count("load.v_in") > 0);

    EXPECT_EQ(result.port_to_signal.at("vin.ext"), result.port_to_signal.at("vin.port"));
    EXPECT_EQ(result.port_to_signal.at("vin.port"), result.port_to_signal.at("load.v_in"));
}

TEST(BridgeLowering, BridgeNodesDoNotEnterScheduler) {
    std::vector<DeviceInstance> devices;
    devices.push_back(make_resistor_device("load"));
    std::vector<BridgePortDefinition> bridges = {
        make_bridge("vin", bp2::BridgeDirection::Input),
        make_bridge("vout", bp2::BridgeDirection::Output),
    };

    std::vector<Connection> connections = {
        {"vin.port", "load.v_in", {}},
        {"load.v_out", "vout.port", {}},
    };

    BuildResult result = build_systems_dev(make_jit_input_from_composite(devices, bridges, connections));

    EXPECT_EQ(result.scheduler.source_count(), 0u);
    EXPECT_EQ(result.scheduler.consumer_count(), 0u);
}

TEST(BridgeLowering, AotFilterRemovesBridgeDevices) {
    std::vector<ResolvedDevice> devices;
    ResolvedDevice resistor = resolve_component(make_resistor_device("load"),
        *test_registry().get("Resistor"));
    devices.push_back(resistor);

    auto filtered = codegen_detail::filter_simulation_devices(devices);

    ASSERT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered.front().classname, "Resistor");
    EXPECT_EQ(filtered.front().name, "load");
}
