#include <gtest/gtest.h>
#include "core/solvers/aot/codegen.h"

// ==...== Domain schedule constants (formerly in legacy solver constants) ==...==
// These are embedded as string literals in generated AOT code and must remain consistent.
namespace DomainSchedule {
    constexpr int MECHANICAL_PERIOD = 3;   // 20 Hz = every 3rd step at 60 Hz
    constexpr int HYDRAULIC_PERIOD = 12;   // 5 Hz = every 12th step at 60 Hz
    constexpr int THERMAL_PERIOD = 60;     // 1 Hz = every 60th step at 60 Hz
    constexpr int CYCLE_LENGTH = 60;        // Least common multiple of all periods
}


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
// Tests: generated header uses push single-pass fields (no accumulators)
// =============================================================================

TEST(CodegenAccumulator, HeaderDoesNotContainAccumulatorFields) {
    auto [devices, connections, port_to_signal, signal_count] = make_multi_domain_devices();

    std::string header = CodeGen::generate_header(
        "test.json", devices, connections, port_to_signal, signal_count);

    EXPECT_EQ(header.find("acc_mechanical_"), std::string::npos)
        << "Header must not declare acc_mechanical_ field in push single-pass mode";
    EXPECT_EQ(header.find("acc_hydraulic_"), std::string::npos)
        << "Header must not declare acc_hydraulic_ field in push single-pass mode";
    EXPECT_EQ(header.find("acc_thermal_"), std::string::npos)
        << "Header must not declare acc_thermal_ field in push single-pass mode";
}

// =============================================================================
// Tests: generated source does not accumulate dt in solve_step
// =============================================================================

TEST(CodegenAccumulator, SolveStepHasNoAccumulatorDtUpdates) {
    auto [devices, connections, port_to_signal, signal_count] = make_multi_domain_devices();

    std::string source = CodeGen::generate_source(
        "test.h", devices, connections, port_to_signal, signal_count);

    EXPECT_EQ(source.find("acc_mechanical_ += dt"), std::string::npos)
        << "solve_step must not update acc_mechanical_ in push single-pass mode";
    EXPECT_EQ(source.find("acc_hydraulic_  += dt"), std::string::npos)
        << "solve_step must not update acc_hydraulic_ in push single-pass mode";
    EXPECT_EQ(source.find("acc_thermal_    += dt"), std::string::npos)
        << "solve_step must not update acc_thermal_ in push single-pass mode";
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


