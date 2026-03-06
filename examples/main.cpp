#include "jit_solver/jit_solver.h"
#include "jit_solver/state.h"
#include <spdlog/spdlog.h>
#include <iostream>
#include <iomanip>
#include <set>
#include <map>

int main() {
    spdlog::info("AN-24 C++ JIT Solver - DC Bus Test");

    // Create DC bus reference at 28V
    an24::DeviceInstance dc_bus;
    dc_bus.name = "dc_bus_1";
    dc_bus.classname = "RefNode";
    dc_bus.params["value"] = "28.0";
    dc_bus.ports["v"] = an24::Port{an24::PortDirection::Out, an24::PortType::V};

    // Create ground reference at 0V
    an24::DeviceInstance gnd;
    gnd.name = "gnd";
    gnd.classname = "RefNode";
    gnd.params["value"] = "0.0";
    gnd.ports["v"] = an24::Port{an24::PortDirection::Out, an24::PortType::V};

    // Create battery
    an24::DeviceInstance battery;
    battery.name = "bat_main_1";
    battery.classname = "Battery";
    battery.params["v_nominal"] = "28.0";
    battery.params["internal_r"] = "0.01";
    battery.ports["v_in"] = an24::Port{an24::PortDirection::In, an24::PortType::V};
    battery.ports["v_out"] = an24::Port{an24::PortDirection::Out, an24::PortType::V};

    // Create a load
    an24::DeviceInstance load;
    load.name = "load_1";
    load.classname = "Resistor";
    load.params["conductance"] = "0.1";
    load.ports["v_in"] = an24::Port{an24::PortDirection::In, an24::PortType::V};
    load.ports["v_out"] = an24::Port{an24::PortDirection::Out, an24::PortType::V};

    std::vector<an24::DeviceInstance> devices = {dc_bus, gnd, battery, load};

    std::vector<std::pair<std::string, std::string>> connections = {
        {"bat_main_1.v_out", "dc_bus_1.v"},
        {"dc_bus_1.v", "load_1.v_in"},
        {"load_1.v_out", "gnd.v"},
    };

    auto result = an24::build_systems_dev(devices, connections);

    std::cout << "=== System ===\n";
    std::cout << "Signals: " << result.signal_count << "\n";
    std::cout << "Components: " << result.systems.component_count() << "\n\n";

    // Print port mapping
    std::map<uint32_t, std::string> signal_to_port;
    std::cout << "=== Port mapping ===\n";
    for (const auto& [port, sig] : result.port_to_signal) {
        std::cout << port << " -> signal " << sig << "\n";
        signal_to_port[sig] = port;
    }
    std::cout << "\n";

    // Allocate state
    an24::SimulationState state;

    // First, find unique signals
    std::set<uint32_t> unique_signals;
    for (const auto& [port, sig] : result.port_to_signal) {
        unique_signals.insert(sig);
    }

    std::cout << "Unique signals: ";
    for (uint32_t s : unique_signals) std::cout << s << " ";
    std::cout << "\n\n";

    // Create mapping from old signal index to new (0-based)
    std::map<uint32_t, uint32_t> signal_remap;
    uint32_t next_idx = 0;
    for (uint32_t old_sig : unique_signals) {
        signal_remap[old_sig] = next_idx++;
    }

    // Add sentinel for unconnected
    uint32_t sentinel_idx = next_idx++;

    std::cout << "Signal remap: ";
    for (const auto& [old_, new_] : signal_remap) {
        std::cout << old_ << "->" << new_ << " ";
    }
    std::cout << ", sentinel=" << sentinel_idx << "\n\n";

    // Find fixed signals
    std::set<uint32_t> fixed_set;
    for (const auto& dev : devices) {
        if (dev.classname == "RefNode") {
            float value = 0.0f;
            auto it = dev.params.find("value");
            if (it != dev.params.end()) {
                value = std::stof(it->second);
            }
            std::string full_port = dev.name + ".v_out";
            auto it_sig = result.port_to_signal.find(full_port);
            if (it_sig != result.port_to_signal.end()) {
                uint32_t new_sig = signal_remap[it_sig->second];
                std::cout << "RefNode " << dev.name << " v_out: old=" << it_sig->second
                          << " new=" << new_sig << " fixed at " << value << " V\n";
                fixed_set.insert(new_sig);
            }
        }
    }

    // Allocate signals
    for (size_t i = 0; i < next_idx; ++i) {
        bool is_fixed = fixed_set.count(i) > 0;
        state.allocate_signal(0.0f, {an24::Domain::Electrical, is_fixed});
    }

    // Set fixed voltages
    for (const auto& dev : devices) {
        if (dev.classname == "RefNode") {
            float value = 0.0f;
            auto it = dev.params.find("value");
            if (it != dev.params.end()) {
                value = std::stof(it->second);
            }
            std::string full_port = dev.name + ".v_out";
            auto it_sig = result.port_to_signal.find(full_port);
            if (it_sig != result.port_to_signal.end()) {
                uint32_t new_sig = signal_remap[it_sig->second];
                state.across[new_sig] = value;
                std::cout << "Set signal " << new_sig << " = " << value << " V\n";
            }
        }
    }

    std::cout << "\n=== Initial voltages ===\n";
    for (size_t i = 0; i < state.across.size(); ++i) {
        std::cout << "signal[" << i << "]: " << state.across[i]
                  << (state.signal_types[i].is_fixed ? " (FIXED)" : "") << "\n";
    }
    std::cout << "\n";

    // Run simulation
    const int STEPS = 100;
    const float omega = 1.5f;

    for (int step = 0; step < STEPS; ++step) {
        state.clear_through();
        result.systems.solve_step(state, step, 1.0f / 60.0f);
        state.precompute_inv_conductance();

        for (size_t i = 0; i < state.across.size(); ++i) {
            if (!state.signal_types[i].is_fixed && state.inv_conductance[i] > 0.0f) {
                state.across[i] += state.through[i] * state.inv_conductance[i] * omega;
            }
        }
    }

    std::cout << "=== After " << STEPS << " steps ===\n";
    for (size_t i = 0; i < state.across.size(); ++i) {
        std::cout << "signal[" << i << "]: " << std::fixed << std::setprecision(2)
                  << state.across[i] << " V";
        if (state.signal_types[i].is_fixed) std::cout << " (FIXED)";
        std::cout << "\n";
    }

    return 0;
}
