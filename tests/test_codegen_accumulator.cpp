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
        dev.ports["v_out"] = {PortDirection::Out, PortType::V, std::nullopt};
        port_to_signal["gnd.v_out"] = next_sig++;
        devices.push_back(std::move(dev));
    }

    // Battery (electrical)
    {
        DeviceInstance dev;
        dev.name = "bat";
        dev.classname = "Battery";
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
