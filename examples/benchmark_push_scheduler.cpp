#include "core/solvers/jit/jit_solver.h"
#include "core/solvers/jit/components/port_registry.h"
#include "core/solvers/jit/state.h"

#include <chrono>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

DeviceInstance make_device(const std::string& name,
                           const std::string& classname,
                           const std::unordered_map<std::string, std::string>& params = {}) {
    DeviceInstance dev;
    dev.name = name;
    dev.classname = classname;
    dev.params = params;
    dev.spec = nullptr;
    for (const auto& p : get_component_ports(classname)) {
        dev.ports[p] = Port{bp2::Direction::InOut, PortType::Any};
    }
    return dev;
}

} // namespace

int main() {
    constexpr int chain_len = 8000;
    constexpr int warmup_steps = 100;
    constexpr int measured_steps = 1000;
    constexpr double dt = 1.0 / 60.0;

    std::vector<DeviceInstance> devices;
    devices.reserve(chain_len + 2);
    devices.push_back(make_device("src", "Value", {{"value", "1.0"}}));
    devices.push_back(make_device("k", "Value", {{"value", "0.5"}}));

    for (int i = 0; i < chain_len; ++i) {
        devices.push_back(make_device("add" + std::to_string(i), "Add"));
    }

    JitBuildInput input;
    input.devices = devices;

    uint32_t next_signal = 0;
    input.port_to_signal["src.o"] = next_signal;
    input.port_to_signal["add0.A"] = next_signal;
    ++next_signal;

    input.port_to_signal["k.o"] = next_signal;
    for (int i = 0; i < chain_len; ++i) {
        input.port_to_signal["add" + std::to_string(i) + ".B"] = next_signal;
    }
    ++next_signal;

    for (int i = 0; i < chain_len; ++i) {
        const std::string curr = "add" + std::to_string(i);
        input.port_to_signal[curr + ".o"] = next_signal;
        if (i + 1 < chain_len) {
            input.port_to_signal["add" + std::to_string(i + 1) + ".A"] = next_signal;
        }
        ++next_signal;
    }

    input.signal_count = next_signal + 1;

    auto result = build_systems_dev(input);

    SimulationState st;
    for (uint32_t i = 0; i < result.signal_count; ++i) {
        (void)st.allocate_signal(0.0f);
    }

    for (int i = 0; i < warmup_steps; ++i) {
        result.scheduler.step(st, dt);
    }

    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < measured_steps; ++i) {
        result.scheduler.step(st, dt);
    }
    const auto t1 = std::chrono::steady_clock::now();

    const double total_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double us_per_step = (total_ms * 1000.0) / static_cast<double>(measured_steps);

    const std::string last = "add" + std::to_string(chain_len - 1) + ".o";
    const float last_value = st.values[result.port_to_signal.at(last)];

    std::cout << "push_benchmark"
              << " components=" << devices.size()
              << " signals=" << result.signal_count
              << " steps=" << measured_steps
              << " total_ms=" << total_ms
              << " us_per_step=" << us_per_step
              << " last_value=" << last_value
              << "\n";

    return 0;
}
