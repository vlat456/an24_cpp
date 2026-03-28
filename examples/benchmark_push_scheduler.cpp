#include "jit_solver/jit_solver.h"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/state.h"

#include <chrono>
#include <iostream>
#include <string>
#include <vector>

namespace {

DeviceInstance make_device(const std::string& name,
                           const std::string& classname,
                           const std::unordered_map<std::string, std::string>& params = {}) {
    DeviceInstance dev;
    dev.name = name;
    dev.classname = classname;
    dev.params = params;
    dev.execution = {};
    for (const auto& p : get_component_ports(classname)) {
        dev.ports[p] = Port{PortDirection::InOut, PortType::Any};
    }
    return dev;
}

} // namespace

int main() {
    constexpr int chain_len = 8000;
    constexpr int warmup_steps = 100;
    constexpr int measured_steps = 1000;
    constexpr float dt = 1.0f / 60.0f;

    std::vector<DeviceInstance> devices;
    devices.reserve(chain_len + 2);
    devices.push_back(make_device("src", "RefNode", {{"value", "1.0"}}));
    devices.push_back(make_device("k", "RefNode", {{"value", "0.5"}}));

    for (int i = 0; i < chain_len; ++i) {
        devices.push_back(make_device("add" + std::to_string(i), "Add"));
    }

    std::vector<std::pair<std::string, std::string>> connections;
    connections.reserve(chain_len * 2);
    for (int i = 0; i < chain_len; ++i) {
        const std::string curr = "add" + std::to_string(i);
        const std::string prev = (i == 0) ? "src.v" : ("add" + std::to_string(i - 1) + ".o");
        connections.emplace_back(prev, curr + ".A");
        connections.emplace_back("k.v", curr + ".B");
    }

    auto result = build_systems_dev(devices, connections);

    SimulationState st;
    for (uint32_t i = 0; i < result.signal_count; ++i) {
        (void)st.allocate_signal(0.0f, {Domain::Electrical, true});
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
