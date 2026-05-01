// =============================================================================
// SimvarProviderHost Tests
// =============================================================================
// Verifies provider partitioning, lifecycle delegation, enabled-state management,
// and frame orchestration.

// Register concrete providers before any build() calls.
#include "simconnect/simconnect_provider.h"

static const bool providers_registered = [] {
    SimConnectProvider::register_type();
    return true;
}();

#include "core/solvers/jit/bridge/simvar_provider_host.h"
#include "simconnect/simconnect_client_stub.h"
#include "simconnect/wire_protocol.h"
#include "simconnect/wire_codec.h"
#include "core/solvers/jit/simulator.h"
#include "core/solvers/jit/jit_build_input.h"
#include "blueprint_v2/interface/direction.h"
#include <algorithm>
#include <gtest/gtest.h>

// =============================================================================
// Helpers
// =============================================================================

static JitBuildInput make_simconnect_build_input() {
    JitBuildInput input;

    // SimConnectInput
    {
        SolverDevice dev;
        dev.name = "temp_sensor";
        dev.classname = "SimConnectInput";
        dev.kind = ComponentKind::SimConnectInput;
        dev.scheduler_role_kind = SchedulerRoleKind::Source;
        dev.params["var_name"] = "AMBIENT TEMPERATURE";
        dev.params["var_type"] = "AVar";
        dev.params["default_value"] = "15.0";
        dev.ports["out"] = Port{bp2::Direction::Output, PortType::Signal, Domain::Logical, true};
        input.devices.push_back(std::move(dev));
    }

    // SimConnectOutput
    {
        SolverDevice dev;
        dev.name = "bus_voltage";
        dev.classname = "SimConnectOutput";
        dev.kind = ComponentKind::SimConnectOutput;
        dev.scheduler_role_kind = SchedulerRoleKind::Consumer;
        dev.params["var_name"] = "ELECTRICAL MAIN BUS VOLTAGE";
        dev.params["var_type"] = "AVar";
        dev.ports["in"] = Port{bp2::Direction::Input, PortType::Signal, Domain::Logical, false};
        input.devices.push_back(std::move(dev));
    }

    input.signal_key_interner.intern("temp_sensor.out");
    input.signal_key_interner.intern("bus_voltage.in");
    input.port_to_signal[input.signal_key_interner.intern("temp_sensor.out")] = 0;
    input.port_to_signal[input.signal_key_interner.intern("bus_voltage.in")] = 1;
    input.signal_count = 2;
    input.initial_values["temp_sensor.out"] = 15.0f;
    input.initial_values["bus_voltage.in"] = 0.0f;

    return input;
}

// ==...== Registry ==...==

TEST(ProviderHostTest, RegisteredTypesReturnsSortedList) {
    auto types = SimvarProviderHost::registered_types();
    ASSERT_GE(types.size(), 1u);
    EXPECT_EQ(types[0], "simconnect");
    EXPECT_TRUE(std::is_sorted(types.begin(), types.end()));
}

// ==...== Enabled state ==...==

TEST(ProviderHostTest, ToggleEnabledChangesState) {
    SimvarProviderHost host;
    EXPECT_FALSE(host.is_enabled("simconnect"));

    bool new_state = host.toggle_enabled("simconnect");
    EXPECT_TRUE(new_state);
    EXPECT_TRUE(host.is_enabled("simconnect"));

    new_state = host.toggle_enabled("simconnect");
    EXPECT_FALSE(new_state);
    EXPECT_FALSE(host.is_enabled("simconnect"));
}

TEST(ProviderHostTest, EnabledStateSurvivesRebuild) {
    SimvarProviderHost host;
    host.toggle_enabled("simconnect");
    ASSERT_TRUE(host.is_enabled("simconnect"));

    // Build with empty input — no providers created, but enabled state persists.
    JitBuildInput empty;
    JIT_Simulator sim;
    sim.start(empty);
    host.build(empty, sim);

    EXPECT_EQ(host.provider_count(), 0u);
    EXPECT_TRUE(host.is_enabled("simconnect"));
}

TEST(ProviderHostTest, EnabledAutoConnectsOnBuild) {
    SimvarProviderHost host;

    // Enable before any providers exist.
    host.toggle_enabled("simconnect");
    ASSERT_TRUE(host.is_enabled("simconnect"));

    // Build with connector nodes — should auto-connect.
    JitBuildInput input = make_simconnect_build_input();
    JIT_Simulator sim;
    sim.start(input);
    host.build(input, sim);

    ASSERT_EQ(host.provider_count(), 1u);
    EXPECT_TRUE(host.is_connected());
}

// ==...== Lifecycle ==...==

TEST(ProviderHostTest, EmptyInputCreatesNoProviders) {
    SimvarProviderHost host;
    JitBuildInput input;
    JIT_Simulator sim;
    sim.start(input);

    host.build(input, sim);
    EXPECT_EQ(host.provider_count(), 0u);
    EXPECT_TRUE(host.connect());      // No providers = vacuously true
    EXPECT_FALSE(host.is_connected()); // No providers = nothing is connected
    host.disconnect();
}

TEST(ProviderHostTest, SimConnectInputOutputRoutesToSimConnectProvider) {
    SimvarProviderHost host;
    JitBuildInput input = make_simconnect_build_input();
    JIT_Simulator sim;
    sim.start(input);

    host.build(input, sim);
    ASSERT_EQ(host.provider_count(), 1u);
    EXPECT_TRUE(host.connect());
    EXPECT_TRUE(host.is_connected());

    // Simulate a frame
    host.poll(0.0);
    host.read_into(sim.values(), static_cast<uint32_t>(sim.get_signal_count()));
    sim.step(0.016);
    host.write_from(sim.values(), static_cast<uint32_t>(sim.get_signal_count()));

    host.disconnect();
    EXPECT_FALSE(host.is_connected());
}

TEST(ProviderHostTest, ReadIntoWithNullPtrDoesNotCrash) {
    SimvarProviderHost host;
    JitBuildInput input;
    JIT_Simulator sim;
    sim.start(input);

    host.build(input, sim);
    host.read_into(nullptr, 0);
    host.write_from(nullptr, 0);
}

TEST(ProviderHostTest, RebuildClearsPreviousProviders) {
    SimvarProviderHost host;
    JIT_Simulator sim;

    JitBuildInput input1 = make_simconnect_build_input();
    sim.start(input1);
    host.build(input1, sim);
    ASSERT_EQ(host.provider_count(), 1u);

    JitBuildInput empty;
    sim.stop();
    sim.start(empty);
    host.build(empty, sim);
    EXPECT_EQ(host.provider_count(), 0u);
}

// ==...== SimConnectProvider adapter ==...==

TEST(ProviderHostTest, SimConnectProviderAdapter) {
    JitBuildInput input = make_simconnect_build_input();

    JIT_Simulator sim;
    sim.start(input);

    SimvarProviderHost host;
    host.build(input, sim);
    ASSERT_EQ(host.provider_count(), 1u);

    EXPECT_TRUE(host.connect());
    EXPECT_TRUE(host.is_connected());

    host.poll(0.0);
    host.read_into(sim.values(), static_cast<uint32_t>(sim.get_signal_count()));
    sim.step(0.016);
    host.write_from(sim.values(), static_cast<uint32_t>(sim.get_signal_count()));

    host.disconnect();
}

// ==...== Direct provider frame loop ==...==

TEST(SimConnectProviderTest, DirectProviderFrameLoop) {
    JitBuildInput input = make_simconnect_build_input();

    JIT_Simulator sim;
    sim.start(input);

    SimConnectProvider provider;
    provider.build(input, sim);
    provider.connect();

    provider.poll(0.0);
    float values[2] = {0.0f, 0.0f};
    provider.read_into(values, 2);

    EXPECT_TRUE(provider.is_connected());
    provider.disconnect();
}

// ==...== Toggle enabled with active providers ==...==

TEST(ProviderHostTest, ToggleEnabledDisconnectsAndReconnectsProvider) {
    SimvarProviderHost host;
    host.toggle_enabled("simconnect");

    JitBuildInput input = make_simconnect_build_input();
    JIT_Simulator sim;
    sim.start(input);
    host.build(input, sim);

    ASSERT_EQ(host.provider_count(), 1u);
    EXPECT_TRUE(host.is_connected());
    EXPECT_TRUE(host.is_type_connected("simconnect"));

    // Disable → disconnects matching provider
    bool new_state = host.toggle_enabled("simconnect");
    EXPECT_FALSE(new_state);
    EXPECT_FALSE(host.is_connected());
    EXPECT_FALSE(host.is_type_connected("simconnect"));
    EXPECT_EQ(host.provider_count(), 1u);  // provider still exists, just disconnected

    // Enable → reconnects matching provider
    new_state = host.toggle_enabled("simconnect");
    EXPECT_TRUE(new_state);
    EXPECT_TRUE(host.is_connected());
    EXPECT_TRUE(host.is_type_connected("simconnect"));
}

// ==...== Teardown clears providers but preserves enabled state ==...==

TEST(ProviderHostTest, TeardownClearsProvidersPreservesEnabled) {
    SimvarProviderHost host;
    host.toggle_enabled("simconnect");

    JitBuildInput input = make_simconnect_build_input();
    JIT_Simulator sim;
    sim.start(input);
    host.build(input, sim);

    ASSERT_EQ(host.provider_count(), 1u);
    EXPECT_TRUE(host.is_connected());

    host.teardown();

    EXPECT_EQ(host.provider_count(), 0u);
    EXPECT_FALSE(host.is_connected());
    // Enabled preference survives teardown
    EXPECT_TRUE(host.is_enabled("simconnect"));
}

// ==...== Teardown is idempotent ==...==

TEST(ProviderHostTest, TeardownIdempotent) {
    SimvarProviderHost host;
    host.teardown();  // No crash on empty host
    EXPECT_EQ(host.provider_count(), 0u);
}

// ==...== is_type_connected returns false for unknown type ==...==

TEST(ProviderHostTest, IsTypeConnectedReturnsFalseForUnknownType) {
    SimvarProviderHost host;
    EXPECT_FALSE(host.is_type_connected("nonexistent"));
}
