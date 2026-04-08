#include <gtest/gtest.h>

#include "core/solvers/aot/codegen_internal.h"
#include "core/solvers/jit/jit_solver.h"
#include "core/solvers/jit/components/port_registry.h"

#include "json_parser/json_parser.h"

namespace {

const TypeRegistry& shared_registry() {
    static const TypeRegistry registry = load_type_registry("library/");
    return registry;
}

DeviceInstance make_bridge_device(std::string name, std::string classname) {
    DeviceInstance dev;
    dev.name = std::move(name);
    dev.classname = std::move(classname);
    const TypeDefinition* def = shared_registry().get(dev.classname);
    EXPECT_NE(def, nullptr);
    return merge_device_instance(dev, *def);
}

DeviceInstance make_resistor_device(std::string name) {
    DeviceInstance dev;
    dev.name = std::move(name);
    dev.classname = "Resistor";
    dev.params["conductance"] = "1.0";
    const TypeDefinition* def = shared_registry().get(dev.classname);
    EXPECT_NE(def, nullptr);
    return merge_device_instance(dev, *def);
}

} // namespace

TEST(BridgeLowering, BuildSkipsBridgeRuntimeComponents) {
    std::vector<DeviceInstance> devices;
    devices.push_back(make_bridge_device("vin", "BlueprintInput"));
    devices.push_back(make_resistor_device("load"));
    devices.push_back(make_bridge_device("vout", "BlueprintOutput"));

    std::vector<std::pair<std::string, std::string>> connections = {
        {"vin.port", "load.v_in"},
        {"load.v_out", "vout.port"},
    };

    BuildResult result = build_systems_dev(devices, connections);

    EXPECT_EQ(result.devices.count("vin"), 0u);
    EXPECT_EQ(result.devices.count("vout"), 0u);
    EXPECT_EQ(result.devices.count("load"), 1u);
}

TEST(BridgeLowering, BridgeSignalsStillUnifiedForAliasContract) {
    std::vector<DeviceInstance> devices;
    devices.push_back(make_bridge_device("vin", "BlueprintInput"));
    devices.push_back(make_resistor_device("load"));

    std::vector<std::pair<std::string, std::string>> connections = {
        {"vin.port", "load.v_in"},
    };

    BuildResult result = build_systems_dev(devices, connections);

    ASSERT_TRUE(result.port_to_signal.count("vin.ext") > 0);
    ASSERT_TRUE(result.port_to_signal.count("vin.port") > 0);
    ASSERT_TRUE(result.port_to_signal.count("load.v_in") > 0);

    EXPECT_EQ(result.port_to_signal.at("vin.ext"), result.port_to_signal.at("vin.port"));
    EXPECT_EQ(result.port_to_signal.at("vin.port"), result.port_to_signal.at("load.v_in"));
}

TEST(BridgeLowering, BridgeNodesDoNotEnterScheduler) {
    std::vector<DeviceInstance> devices;
    devices.push_back(make_bridge_device("vin", "BlueprintInput"));
    devices.push_back(make_resistor_device("load"));
    devices.push_back(make_bridge_device("vout", "BlueprintOutput"));

    std::vector<std::pair<std::string, std::string>> connections = {
        {"vin.port", "load.v_in"},
        {"load.v_out", "vout.port"},
    };

    BuildResult result = build_systems_dev(devices, connections);

    EXPECT_EQ(result.scheduler.source_count(), 0u);
    EXPECT_EQ(result.scheduler.consumer_count(), 0u);
}

TEST(BridgeLowering, AotFilterRemovesBridgeDevices) {
    std::vector<DeviceInstance> devices;

    DeviceInstance bridge_in = make_bridge_device("vin", "BlueprintInput");
    DeviceInstance bridge_out = make_bridge_device("vout", "BlueprintOutput");
    DeviceInstance resistor = make_resistor_device("load");

    devices.push_back(bridge_in);
    devices.push_back(resistor);
    devices.push_back(bridge_out);

    auto filtered = codegen_detail::filter_simulation_devices(devices);

    ASSERT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered.front().classname, "Resistor");
    EXPECT_EQ(filtered.front().name, "load");
}
