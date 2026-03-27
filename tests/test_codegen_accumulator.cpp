#include <gtest/gtest.h>
#include "codegen/codegen.h"
#include "jit_solver/SOR_constants.h"


// =============================================================================
// Helper: build a minimal device set with multi-domain components
// =============================================================================
static auto make_multi_domain_devices() {
    std::vector<DeviceInstance> devices;
    std::unordered_map<std::string, uint32_t> port_to_signal;
    uint32_t next_sig = 0;

    // RefNode (ground)
    {
        DeviceInstance dev;
        dev.name = "gnd";
        dev.classname = "RefNode";
        ExecutionPhases phases;
        phases.electrical_passive = true;
        dev.execution = phases;
        dev.ports["v_out"] = {PortDirection::Out, PortType::V, std::nullopt};
        port_to_signal["gnd.v_out"] = next_sig++;
        devices.push_back(std::move(dev));
    }

    // Battery (electrical)
    {
        DeviceInstance dev;
        dev.name = "bat";
        dev.classname = "Battery";
        ExecutionPhases phases;
        phases.electrical_passive = true;
        dev.execution = phases;
        dev.params["domain"] = "Electrical";
        dev.params["emf"] = "28";
        dev.params["internal_r"] = "0.05";
        dev.ports["v_in"] = {PortDirection::In, PortType::V, std::nullopt};
        dev.ports["v_out"] = {PortDirection::Out, PortType::V, std::nullopt};
        port_to_signal["bat.v_in"] = next_sig++;
        port_to_signal["bat.v_out"] = next_sig++;
        devices.push_back(std::move(dev));
    }

    // Radiator (thermal domain)
    {
        DeviceInstance dev;
        dev.name = "rad";
        dev.classname = "Radiator";
        ExecutionPhases phases;
        phases.electrical_passive = true;
        phases.thermal = true;
        dev.execution = phases;
        dev.params["domain"] = "Electrical,Thermal";
        dev.ports["v_in"] = {PortDirection::In, PortType::V, std::nullopt};
        dev.ports["v_out"] = {PortDirection::Out, PortType::V, std::nullopt};
        port_to_signal["rad.v_in"] = next_sig++;
        port_to_signal["rad.v_out"] = next_sig++;
        devices.push_back(std::move(dev));
    }

    // ElectricPump (mechanical + hydraulic)
    {
        DeviceInstance dev;
        dev.name = "pump";
        dev.classname = "ElectricPump";
        ExecutionPhases phases;
        phases.electrical_passive = true;
        phases.mechanical = true;
        phases.hydraulic = true;
        dev.execution = phases;
        dev.params["domain"] = "Electrical,Mechanical,Hydraulic";
        dev.ports["v_in"] = {PortDirection::In, PortType::V, std::nullopt};
        dev.ports["v_out"] = {PortDirection::Out, PortType::V, std::nullopt};
        port_to_signal["pump.v_in"] = next_sig++;
        port_to_signal["pump.v_out"] = next_sig++;
        devices.push_back(std::move(dev));
    }

    struct Result {
        std::vector<DeviceInstance> devices;
        std::vector<Connection> connections;
        std::unordered_map<std::string, uint32_t> port_to_signal;
        uint32_t signal_count;
    };

    return Result{
        std::move(devices),
        {},  // no connections needed for codegen output tests
        std::move(port_to_signal),
        next_sig
    };
}

// =============================================================================
// Tests: generated header contains accumulator fields
// =============================================================================

TEST(CodegenAccumulator, HeaderContainsAccumulatorFields) {
    auto [devices, connections, port_to_signal, signal_count] = make_multi_domain_devices();

    std::string header = CodeGen::generate_header(
        "test.json", devices, connections, port_to_signal, signal_count);

    EXPECT_NE(header.find("acc_mechanical_"), std::string::npos)
        << "Header must declare acc_mechanical_ field";
    EXPECT_NE(header.find("acc_hydraulic_"), std::string::npos)
        << "Header must declare acc_hydraulic_ field";
    EXPECT_NE(header.find("acc_thermal_"), std::string::npos)
        << "Header must declare acc_thermal_ field";
}

// =============================================================================
// Tests: generated source accumulates dt in solve_step
// =============================================================================

TEST(CodegenAccumulator, SolveStepAccumulatesDt) {
    auto [devices, connections, port_to_signal, signal_count] = make_multi_domain_devices();

    std::string source = CodeGen::generate_source(
        "test.h", devices, connections, port_to_signal, signal_count);

    // solve_step must accumulate dt into all three accumulators
    EXPECT_NE(source.find("acc_mechanical_ += dt"), std::string::npos)
        << "solve_step must accumulate dt into acc_mechanical_";
    EXPECT_NE(source.find("acc_hydraulic_  += dt"), std::string::npos)
        << "solve_step must accumulate dt into acc_hydraulic_";
    EXPECT_NE(source.find("acc_thermal_    += dt"), std::string::npos)
        << "solve_step must accumulate dt into acc_thermal_";
}

// =============================================================================
// Tests: no dt*N.0f pattern in generated code (old broken pattern)
// =============================================================================

TEST(CodegenAccumulator, NoDtMultiplyPatternInGenerated) {
    auto [devices, connections, port_to_signal, signal_count] = make_multi_domain_devices();

    std::string source = CodeGen::generate_source(
        "test.h", devices, connections, port_to_signal, signal_count);

    EXPECT_EQ(source.find("dt * 3.0f"), std::string::npos)
        << "Generated code must NOT use dt * 3.0f (old pattern)";
    EXPECT_EQ(source.find("dt * 12.0f"), std::string::npos)
        << "Generated code must NOT use dt * 12.0f (old pattern)";
    EXPECT_EQ(source.find("dt * 60.0f"), std::string::npos)
        << "Generated code must NOT use dt * 60.0f (old pattern)";
}

// =============================================================================
// Tests: mechanical uses accumulator and resets
// =============================================================================

TEST(CodegenAccumulator, MechanicalUsesAccumulatorAndResets) {
    auto [devices, connections, port_to_signal, signal_count] = make_multi_domain_devices();

    std::string source = CodeGen::generate_source(
        "test.h", devices, connections, port_to_signal, signal_count);

    // Mechanical solver must receive acc_mechanical_
    EXPECT_NE(source.find("solve_mechanical(*st, acc_mechanical_)"), std::string::npos)
        << "Mechanical solver must receive accumulated dt, not dt*3";

    // Must reset after use
    EXPECT_NE(source.find("acc_mechanical_ = 0.0f"), std::string::npos)
        << "acc_mechanical_ must be reset after mechanical solve";
}

TEST(CodegenAccumulator, HydraulicUsesAccumulatorAndResets) {
    auto [devices, connections, port_to_signal, signal_count] = make_multi_domain_devices();

    std::string source = CodeGen::generate_source(
        "test.h", devices, connections, port_to_signal, signal_count);

    EXPECT_NE(source.find("solve_hydraulic(*st, acc_hydraulic_)"), std::string::npos)
        << "Hydraulic solver must receive accumulated dt, not dt*12";

    EXPECT_NE(source.find("acc_hydraulic_ = 0.0f"), std::string::npos)
        << "acc_hydraulic_ must be reset after hydraulic solve";
}

TEST(CodegenAccumulator, ThermalUsesAccumulatorAndResets) {
    auto [devices, connections, port_to_signal, signal_count] = make_multi_domain_devices();

    std::string source = CodeGen::generate_source(
        "test.h", devices, connections, port_to_signal, signal_count);

    EXPECT_NE(source.find("solve_thermal(*st, acc_thermal_)"), std::string::npos)
        << "Thermal solver must receive accumulated dt, not dt*60";

    EXPECT_NE(source.find("acc_thermal_ = 0.0f"), std::string::npos)
        << "acc_thermal_ must be reset after thermal solve";
}

// =============================================================================
// Tests: DomainSchedule constants are consistent
// =============================================================================

TEST(CodegenAccumulator, DomainScheduleConstantsAreCanonical) {
    EXPECT_EQ(DomainSchedule::MECHANICAL_PERIOD, 3);
    EXPECT_EQ(DomainSchedule::HYDRAULIC_PERIOD, 12);
    EXPECT_EQ(DomainSchedule::THERMAL_PERIOD, 60);
    EXPECT_EQ(DomainSchedule::CYCLE_LENGTH, 60);
    // CYCLE_LENGTH must be divisible by all periods
    EXPECT_EQ(DomainSchedule::CYCLE_LENGTH % DomainSchedule::MECHANICAL_PERIOD, 0);
    EXPECT_EQ(DomainSchedule::CYCLE_LENGTH % DomainSchedule::HYDRAULIC_PERIOD, 0);
    EXPECT_EQ(DomainSchedule::CYCLE_LENGTH % DomainSchedule::THERMAL_PERIOD, 0);
}

// =============================================================================
// Tests: dispatch table uses CYCLE_LENGTH entries
// =============================================================================

TEST(CodegenAccumulator, DispatchTableSizeMatchesCycleLength) {
    auto [devices, connections, port_to_signal, signal_count] = make_multi_domain_devices();

    std::string source = CodeGen::generate_source(
        "test.h", devices, connections, port_to_signal, signal_count);

    // The dispatch table declaration should contain CYCLE_LENGTH entries
    std::string expected_table = "dispatch_table[" + std::to_string(DomainSchedule::CYCLE_LENGTH) + "]";
    EXPECT_NE(source.find(expected_table), std::string::npos)
        << "Dispatch table must have " << DomainSchedule::CYCLE_LENGTH << " entries";

    // step_0 through step_(CYCLE_LENGTH-1) must exist
    std::string last_step = "step_" + std::to_string(DomainSchedule::CYCLE_LENGTH - 1);
    EXPECT_NE(source.find(last_step), std::string::npos)
        << "Last step method step_" << DomainSchedule::CYCLE_LENGTH - 1 << " must be generated";
}

// =============================================================================
// Regression: AOT SOR must use dynamic signal count (JIT parity)
// =============================================================================

TEST(CodegenAccumulator, SorUsesDynamicSignalCount) {
    auto [devices, connections, port_to_signal, signal_count] = make_multi_domain_devices();

    std::string source = CodeGen::generate_source(
        "test.h", devices, connections, port_to_signal, signal_count);

    EXPECT_NE(source.find("st->dynamic_signals_count"), std::string::npos)
        << "AOT solve_sor_iteration must use st->dynamic_signals_count";

    EXPECT_EQ(source.find("solve_sor_iteration(st->across.data(), st->through.data(), st->inv_conductance.data(), SIGNAL_COUNT"),
              std::string::npos)
        << "AOT solve_sor_iteration must not iterate over SIGNAL_COUNT";
}

TEST(CodegenAccumulator, ControlCommitPhaseIsEmittedBeforeSecondElectricalPass) {
    std::vector<DeviceInstance> devices;
    std::unordered_map<std::string, uint32_t> port_to_signal;
    uint32_t next_sig = 0;

    // HoldButton (control-commit participant)
    {
        DeviceInstance dev;
        dev.name = "btn";
        dev.classname = "HoldButton";
        ExecutionPhases phases;
        phases.control_commit = true;
        dev.execution = phases;
        dev.params["domain"] = "Electrical";
        dev.ports["v_in"] = {PortDirection::In, PortType::V, std::nullopt};
        dev.ports["v_out"] = {PortDirection::Out, PortType::V, std::nullopt};
        dev.ports["control"] = {PortDirection::In, PortType::V, std::nullopt};
        dev.ports["state"] = {PortDirection::Out, PortType::I, std::nullopt};
        port_to_signal["btn.v_in"] = next_sig++;
        port_to_signal["btn.v_out"] = next_sig++;
        port_to_signal["btn.control"] = next_sig++;
        port_to_signal["btn.state"] = next_sig++;
        devices.push_back(std::move(dev));
    }

    // ControlledVoltageSource (actuator participant)
    {
        DeviceInstance dev;
        dev.name = "cvs";
        dev.classname = "ControlledVoltageSource";
        ExecutionPhases phases;
        phases.electrical_actuator = true;
        dev.execution = phases;
        dev.params["domain"] = "Electrical";
        dev.ports["cmd"] = {PortDirection::In, PortType::I, std::nullopt};
        dev.ports["v_neg"] = {PortDirection::In, PortType::V, std::nullopt};
        dev.ports["v_pos"] = {PortDirection::Out, PortType::V, std::nullopt};
        port_to_signal["cvs.cmd"] = next_sig++;
        port_to_signal["cvs.v_neg"] = next_sig++;
        port_to_signal["cvs.v_pos"] = next_sig++;
        devices.push_back(std::move(dev));
    }

    std::string source = CodeGen::generate_source("test.h", devices, {}, port_to_signal, next_sig);

    const std::string step0_begin = "step_0(void* state, float dt)";
    const std::string step1_begin = "step_1(void* state, float dt)";
    size_t block_begin = source.find(step0_begin);
    size_t block_end = source.find(step1_begin);
    ASSERT_NE(block_begin, std::string::npos) << "Generated source must contain step_0";
    ASSERT_NE(block_end, std::string::npos) << "Generated source must contain step_1";
    ASSERT_LT(block_begin, block_end);
    const std::string step0 = source.substr(block_begin, block_end - block_begin);

    const std::string commit_call = "btn.commit_control(*st, dt);";
    const std::string second_pass_label = "// Phase 6: second electrical pass (passive + actuators)";
    const std::string phase8_label = "// Phase 8: finalize";
    const std::string finalize_call = "btn.finalize_step(*st, dt);";

    size_t commit_pos = step0.find(commit_call);
    size_t second_pass_pos = step0.find(second_pass_label);
    size_t phase8_pos = step0.find(phase8_label);
    size_t finalize_btn_pos = step0.find(finalize_call);

    ASSERT_NE(commit_pos, std::string::npos)
        << "Generated source must call commit_control for HoldButton";
    ASSERT_NE(second_pass_pos, std::string::npos)
        << "Generated source must contain second electrical pass phase";
    ASSERT_NE(phase8_pos, std::string::npos)
        << "Generated source must contain finalize phase";
    EXPECT_LT(commit_pos, second_pass_pos)
        << "control_commit must run before actuator electrical pass";
    EXPECT_EQ(finalize_btn_pos, std::string::npos)
        << "HoldButton must not be emitted in finalize when control_commit is present";
}
