#include "core/solvers/jit/bridge/mock_provider.h"
#include "core/solvers/jit/jit_build_input.h"
#include "core/solvers/jit/simulator.h"
#include "blueprint_v2/interface/direction.h"
#include <gtest/gtest.h>

// =============================================================================
// MockProvider Tests
// =============================================================================
// Verifies that the mock provider correctly scans SimConnectInput/SimConnectOutput
// nodes, injects mock values into the signal array, and captures output values.

static JitBuildInput make_mock_build_input() {
    JitBuildInput input;

    // SimConnectInput (signal index 0)
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

    // SimConnectOutput (signal index 1)
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

    // Non-SimVar node (should be ignored)
    {
        SolverDevice dev;
        dev.name = "adder";
        dev.classname = "Add";
        dev.kind = ComponentKind::Add;
        dev.scheduler_role_kind = SchedulerRoleKind::Consumer;
        dev.ports["a"] = Port{bp2::Direction::Input, PortType::Signal, Domain::Logical, false};
        dev.ports["b"] = Port{bp2::Direction::Input, PortType::Signal, Domain::Logical, false};
        dev.ports["out"] = Port{bp2::Direction::Output, PortType::Signal, Domain::Logical, true};
        input.devices.push_back(std::move(dev));
    }

    input.signal_key_interner.intern("temp_sensor.out");
    input.signal_key_interner.intern("bus_voltage.in");
    input.signal_key_interner.intern("adder.out");
    input.port_to_signal[input.signal_key_interner.intern("temp_sensor.out")] = 0;
    input.port_to_signal[input.signal_key_interner.intern("bus_voltage.in")] = 1;
    input.port_to_signal[input.signal_key_interner.intern("adder.out")] = 2;
    input.signal_count = 3;
    input.initial_values["temp_sensor.out"] = 15.0f;
    input.initial_values["bus_voltage.in"] = 0.0f;

    return input;
}

// ==...== Interface & Lifecycle ==...==

TEST(MockProviderTest, NameIsMock) {
    MockProvider provider;
    EXPECT_STREQ(provider.name(), "Mock");
}

TEST(MockProviderTest, ConnectDisconnect) {
    MockProvider provider;
    EXPECT_FALSE(provider.is_connected());
    EXPECT_TRUE(provider.connect());
    EXPECT_TRUE(provider.is_connected());
    provider.disconnect();
    EXPECT_FALSE(provider.is_connected());
}

TEST(MockProviderTest, PollIsNoOp) {
    MockProvider provider;
    provider.poll(1.0);  // Should not crash
}

// ==...== Build & Mapping ==...==

TEST(MockProviderTest, BuildMapsInputAndOutput) {
    MockProvider provider;
    JitBuildInput input = make_mock_build_input();
    JIT_Simulator sim;
    sim.start(input);

    provider.build(input, sim);

    EXPECT_EQ(provider.input_count(), 1u);
    EXPECT_EQ(provider.output_count(), 1u);
}

TEST(MockProviderTest, BuildIgnoresNonSimVarNodes) {
    MockProvider provider;
    JitBuildInput input = make_mock_build_input();
    JIT_Simulator sim;
    sim.start(input);

     provider.build(input, sim);

    // Only SimConnectInput and SimConnectOutput should be mapped
    EXPECT_EQ(provider.input_count(), 1u);
    EXPECT_EQ(provider.output_count(), 1u);
}

TEST(MockProviderTest, BuildWithNoSimVarNodes) {
    MockProvider provider;
    JitBuildInput input;
    JIT_Simulator sim;
    sim.start(input);

    provider.build(input, sim);

    EXPECT_EQ(provider.input_count(), 0u);
    EXPECT_EQ(provider.output_count(), 0u);
}

TEST(MockProviderTest, BuildIsIdempotent) {
    MockProvider provider;
    JitBuildInput input = make_mock_build_input();
    JIT_Simulator sim;
    sim.start(input);

    provider.build(input, sim);
    ASSERT_EQ(provider.input_count(), 1u);
    ASSERT_EQ(provider.output_count(), 1u);

    // Set a mock value, then rebuild — all state should reset
    provider.set_input(0, 99.0f);
    provider.build(input, sim);

    EXPECT_EQ(provider.input_count(), 1u);
    EXPECT_EQ(provider.output_count(), 1u);

    // After rebuild, mock_inputs_ was cleared — default value should apply
    float values[3] = {0.0f, 0.0f, 0.0f};
    provider.read_into(values, 3);
    EXPECT_FLOAT_EQ(values[0], 15.0f);  // default_value from blueprint, not 99.0f
}

// ==...== read_into ==...==

TEST(MockProviderTest, ReadIntoWritesMockValue) {
    MockProvider provider;
    JitBuildInput input = make_mock_build_input();
    JIT_Simulator sim;
    sim.start(input);
    provider.build(input, sim);

    provider.set_input(0, 42.0f);

    float values[3] = {0.0f, 0.0f, 0.0f};
    provider.read_into(values, 3);

    EXPECT_FLOAT_EQ(values[0], 42.0f);
}

TEST(MockProviderTest, ReadIntoFallsBackToDefaultValue) {
    MockProvider provider;
    JitBuildInput input = make_mock_build_input();
    JIT_Simulator sim;
    sim.start(input);
    provider.build(input, sim);

    // Do NOT call set_input — should use default_value from blueprint (15.0)
    float values[3] = {0.0f, 0.0f, 0.0f};
    provider.read_into(values, 3);

    EXPECT_FLOAT_EQ(values[0], 15.0f);
}

TEST(MockProviderTest, ReadIntoRespectsCountBounds) {
    MockProvider provider;
    JitBuildInput input = make_mock_build_input();
    JIT_Simulator sim;
    sim.start(input);
    provider.build(input, sim);

    provider.set_input(0, 99.0f);

    // count=0: should not write anything
    float values[3] = {0.0f, 0.0f, 0.0f};
    provider.read_into(values, 0);

    EXPECT_FLOAT_EQ(values[0], 0.0f);
}

TEST(MockProviderTest, ReadIntoWithNullDoesNotCrash) {
    MockProvider provider;
    JitBuildInput input = make_mock_build_input();
    JIT_Simulator sim;
    sim.start(input);
    provider.build(input, sim);

    provider.set_input(0, 99.0f);

    // nullptr + non-zero count: should not crash
    provider.read_into(nullptr, 3);
    // nullptr + zero count: should not crash
    provider.read_into(nullptr, 0);
}

TEST(MockProviderTest, WriteFromWithNullDoesNotCrash) {
    MockProvider provider;
    JitBuildInput input = make_mock_build_input();
    JIT_Simulator sim;
    sim.start(input);
    provider.build(input, sim);

    // nullptr + non-zero count: should not crash
    provider.write_from(nullptr, 3);
    // nullptr + zero count: should not crash
    provider.write_from(nullptr, 0);
}

// ==...== write_from ==...==

TEST(MockProviderTest, WriteFromCapturesOutputValue) {
    MockProvider provider;
    JitBuildInput input = make_mock_build_input();
    JIT_Simulator sim;
    sim.start(input);
    provider.build(input, sim);

    float values[3] = {0.0f, 3.14f, 0.0f};
    provider.write_from(values, 3);

    EXPECT_FLOAT_EQ(provider.get_output_f(1), 3.14f);
}

TEST(MockProviderTest, WriteFromRespectsCountBounds) {
    MockProvider provider;
    JitBuildInput input = make_mock_build_input();
    JIT_Simulator sim;
    sim.start(input);
    provider.build(input, sim);

    float values[3] = {0.0f, 3.14f, 0.0f};
    provider.write_from(values, 1);  // count=1, signal_index 1 is out of bounds

    EXPECT_FLOAT_EQ(provider.get_output_f(1), 0.0f);
}

TEST(MockProviderTest, WriteFromReturnsZeroForUnknownIndex) {
    MockProvider provider;
    JitBuildInput input = make_mock_build_input();
    JIT_Simulator sim;
    sim.start(input);
    provider.build(input, sim);

    EXPECT_FLOAT_EQ(provider.get_output_f(999), 0.0f);
}

// ==...== Round-trip ==...==

TEST(MockProviderTest, RoundTripPreservesValue) {
    MockProvider provider;
    JitBuildInput input = make_mock_build_input();
    JIT_Simulator sim;
    sim.start(input);
    provider.build(input, sim);

    // Set an input value
    provider.set_input(0, 123.0f);

    // Simulate a frame
    float values[3] = {0.0f, 0.0f, 0.0f};
    provider.read_into(values, 3);
    EXPECT_FLOAT_EQ(values[0], 123.0f);

    // "Simulate" processing: copy input to output
    values[1] = values[0];

    provider.write_from(values, 3);
    EXPECT_FLOAT_EQ(provider.get_output_f(1), 123.0f);
}

// ==...== Typed conversion ==...==

static JitBuildInput make_typed_build_input() {
    JitBuildInput input;

    // SimConnectInput with Int32 type (signal index 0)
    {
        SolverDevice dev;
        dev.name = "int_sensor";
        dev.classname = "SimConnectInput";
        dev.kind = ComponentKind::SimConnectInput;
        dev.scheduler_role_kind = SchedulerRoleKind::Source;
        dev.params["var_name"] = "SOME INT VAR";
        dev.params["var_type"] = "AVar";
        dev.params["val_type"] = "Int32";
        dev.params["default_value"] = "42";
        dev.ports["out"] = Port{bp2::Direction::Output, PortType::Signal, Domain::Logical, true};
        input.devices.push_back(std::move(dev));
    }

    // SimConnectOutput with Bool type (signal index 1)
    {
        SolverDevice dev;
        dev.name = "bool_switch";
        dev.classname = "SimConnectOutput";
        dev.kind = ComponentKind::SimConnectOutput;
        dev.scheduler_role_kind = SchedulerRoleKind::Consumer;
        dev.params["var_name"] = "SOME BOOL VAR";
        dev.params["var_type"] = "AVar";
        dev.params["val_type"] = "Bool";
        dev.ports["in"] = Port{bp2::Direction::Input, PortType::Signal, Domain::Logical, false};
        input.devices.push_back(std::move(dev));
    }

    input.signal_key_interner.intern("int_sensor.out");
    input.signal_key_interner.intern("bool_switch.in");
    input.port_to_signal[input.signal_key_interner.intern("int_sensor.out")] = 0;
    input.port_to_signal[input.signal_key_interner.intern("bool_switch.in")] = 1;
    input.signal_count = 2;

    return input;
}

TEST(MockProviderTest, Int32InputConvertsToFloat) {
    MockProvider provider;
    JitBuildInput input = make_typed_build_input();
    JIT_Simulator sim;
    sim.start(input);
    provider.build(input, sim);

    provider.set_input(0, int32_t{123});

    float values[2] = {0.0f, 0.0f};
    provider.read_into(values, 2);

    EXPECT_FLOAT_EQ(values[0], 123.0f);
}

TEST(MockProviderTest, BoolInputConvertsToFloat) {
    MockProvider provider;
    JitBuildInput input = make_typed_build_input();
    JIT_Simulator sim;
    sim.start(input);
    provider.build(input, sim);

    provider.set_input(0, true);  // signal 0 is Int32, but bool should convert

    float values[2] = {0.0f, 0.0f};
    provider.read_into(values, 2);

    // set_input(bool) sets b=true, to_float reads b and returns 1.0f
    EXPECT_FLOAT_EQ(values[0], 1.0f);
}

TEST(MockProviderTest, FloatOutputConvertsToInt32) {
    MockProvider provider;
    JitBuildInput input = make_typed_build_input();
    JIT_Simulator sim;
    sim.start(input);
    provider.build(input, sim);

    float values[2] = {0.0f, 99.7f};
    provider.write_from(values, 2);

    // signal 0 is Int32 input — not written by write_from
    // signal 1 is Bool output — values[1]=99.7f > 0.5f → true
    EXPECT_TRUE(provider.get_output_b(1));
}

TEST(MockProviderTest, SignalTypeQueryReturnsCorrectType) {
    MockProvider provider;
    JitBuildInput input = make_typed_build_input();
    JIT_Simulator sim;
    sim.start(input);
    provider.build(input, sim);

    auto t0 = provider.signal_type(0);
    ASSERT_TRUE(t0.has_value());
    EXPECT_EQ(*t0, SignalType::Int32);

    auto t1 = provider.signal_type(1);
    ASSERT_TRUE(t1.has_value());
    EXPECT_EQ(*t1, SignalType::Bool);

    auto t2 = provider.signal_type(999);
    EXPECT_FALSE(t2.has_value());
}
