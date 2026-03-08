#include "jit_solver/jit_solver.h"
#include "jit_solver/state.h"
#include "jit_solver/SOR_constants.h"
#include <spdlog/spdlog.h>
#include <iostream>
#include <iomanip>

using namespace an24;

int main() {
    spdlog::set_level(spdlog::level::debug);

    // Test: Battery -> Load -> Ground (no relay, just wire)
    // Expected: bus voltage should be close to 28V (battery nominal)
    DeviceInstance gnd;
    gnd.name = "gnd";
    gnd.classname = "RefNode";
    gnd.params = {{"value", "0.0"}};
    gnd.ports = {{"v", {PortDirection::Out}}};

    DeviceInstance battery;
    battery.name = "battery";
    battery.classname = "Battery";
    battery.params = {{"v_nominal", "28.0"}, {"internal_r", "0.1"}};
    battery.ports = {{"v_in", {PortDirection::In}}, {"v_out", {PortDirection::Out}}};

    DeviceInstance load;
    load.name = "load";
    load.classname = "Resistor";
    load.params = {{"conductance", "0.1"}};
    load.ports = {{"v_in", {PortDirection::In}}, {"v_out", {PortDirection::Out}}};

    std::vector<DeviceInstance> devices = {gnd, battery, load};
    std::vector<std::pair<std::string, std::string>> conn = {
        {"battery.v_out", "load.v_in"},  // battery + to load
        {"load.v_out", "gnd.v"},         // load to ground
        {"battery.v_in", "gnd.v"},       // battery - to ground
    };

    auto result = build_systems_dev(devices, conn);

    std::cout << "=== Port Mapping ===\n";
    for (const auto& [port, sig_id] : result.port_to_signal) {
        std::cout << port << " -> signal " << sig_id << "\n";
    }
    std::cout << "\nSignals: " << result.signal_count << "\n";
    std::cout << "Components: " << result.systems.component_count() << "\n";

    // Allocate state
    SimulationState state;
    for (size_t i = 0; i < result.signal_count; ++i) {
        bool is_fixed = false;
        for (uint32_t fixed : result.fixed_signals) {
            if (static_cast<uint32_t>(i) == fixed) {
                is_fixed = true;
                break;
            }
        }
        state.allocate_signal(0.0f, {Domain::Electrical, is_fixed});
    }

    // Set fixed signal values
    for (const auto& dev : devices) {
        if (dev.classname == "RefNode") {
            float value = 0.0f;
            auto it_val = dev.params.find("value");
            if (it_val != dev.params.end()) {
                value = std::stof(it_val->second);
            }
            std::string port = dev.name + ".v";
            auto it_sig = result.port_to_signal.find(port);
            if (it_sig != result.port_to_signal.end()) {
                state.across[it_sig->second] = value;
                spdlog::debug("[debug] Set {} = {}V", port, value);
            }
        }
    }

    std::cout << "\n=== Initial ===\n";
    for (size_t i = 0; i < state.across.size(); ++i) {
        std::cout << "signal[" << i << "]: " << state.across[i];
        if (state.signal_types[i].is_fixed) std::cout << " (FIXED)";
        std::cout << "\n";
    }

    // Step 1: solve
    std::cout << "\n=== Step 1: After solve ===\n";
    state.clear_through();
    result.systems.solve_step(state, 0, 1.0f / 60.0f);

    std::cout << "through: ";
    for (size_t i = 0; i < state.through.size(); ++i) {
        std::cout << state.through[i] << " ";
    }
    std::cout << "\nconductance: ";
    for (size_t i = 0; i < state.conductance.size(); ++i) {
        std::cout << state.conductance[i] << " ";
    }
    std::cout << "\n";

    state.precompute_inv_conductance();
    std::cout << "inv_conductance: ";
    for (size_t i = 0; i < state.inv_conductance.size(); ++i) {
        std::cout << state.inv_conductance[i] << " ";
    }
    std::cout << "\n";

    // SOR iterations
    const float omega = SOR::OMEGA;
    for (int iter = 0; iter < 10; ++iter) {
        for (size_t i = 0; i < state.across.size(); ++i) {
            if (!state.signal_types[i].is_fixed && state.inv_conductance[i] > 0.0f) {
                float delta = state.through[i] * state.inv_conductance[i] * omega;
                state.across[i] += delta;
            }
        }
        state.clear_through();
        result.systems.solve_step(state, iter + 1, 1.0f / 60.0f);
    }

    std::cout << "\n=== After SOR ===\n";
    for (size_t i = 0; i < state.across.size(); ++i) {
        std::cout << "signal[" << i << "]: " << state.across[i];
        if (state.signal_types[i].is_fixed) std::cout << " (FIXED)";
        std::cout << "\n";
    }

    return 0;
}
