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
    DeviceInstance dc_bus;
    dc_bus.name = "dc_bus_1";
    dc_bus.classname = "RefNode";
    dc_bus.params["value"] = "28.0";
    dc_bus.ports["v"] = Port{PortDirection::Out, PortType::V};

    // Create ground reference at 0V
    DeviceInstance gnd;
    gnd.name = "gnd";
    gnd.classname = "RefNode";
    gnd.params["value"] = "0.0";
    gnd.ports["v"] = Port{PortDirection::Out, PortType::V};

    // Create battery
    DeviceInstance battery;
    battery.name = "bat_main_1";
    battery.classname = "Battery";
    battery.params["v_nominal"] = "28.0";
    battery.params["internal_r"] = "0.01";
    battery.ports["v_in"] = Port{PortDirection::In, PortType::V};
    battery.ports["v_out"] = Port{PortDirection::Out, PortType::V};

    // Create a load
    DeviceInstance load;
    load.name = "load_1";
    load.classname = "Resistor";
    load.params["conductance"] = "0.1";
    load.ports["v_in"] = Port{PortDirection::In, PortType::V};
    load.ports["v_out"] = Port{PortDirection::Out, PortType::V};

    std::vector<DeviceInstance> devices = {dc_bus, gnd, battery, load};

    std::vector<std::pair<std::string, std::string>> connections = {
        {"bat_main_1.v_out", "dc_bus_1.v"},
        {"dc_bus_1.v", "load_1.v_in"},
        {"load_1.v_out", "gnd.v"},
    };

    auto result = build_systems_dev(devices, connections);

    std::cout << "=== System ===\n";
    std::cout << "Signals: " << result.signal_count << "\n";
    std::cout << "Fixed signals: " << result.fixed_signals.size() << "\n\n";

    // Print port mapping
    std::map<uint32_t, std::string> signal_to_port;
    std::cout << "=== Port mapping ===\n";
    for (const auto& [port, sig] : result.port_to_signal) {
        std::cout << port << " -> signal " << sig << "\n";
        signal_to_port[sig] = port;
    }
    std::cout << "\n";

    // Allocate state
    SimulationState state;

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
        state.allocate_signal(0.0f, {Domain::Electrical, is_fixed});
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

    std::cout << "\n=== AOT-only Mode ===\n";
    std::cout << "Component simulation is codegen'd, not runtime managed.\n";
    std::cout << "Port mapping computed for AOT code generator.\n";

    return 0;
}
