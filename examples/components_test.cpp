#include "jit_solver/jit_solver.h"
#include "jit_solver/state.h"
#include <spdlog/spdlog.h>
#include <iostream>
#include <iomanip>
#include <map>
#include <set>

using namespace an24;

// Helper: build state from result with proper signal remapping
struct SimState {
    SimulationState state;
    std::map<uint32_t, uint32_t> signal_remap;  // old -> new
    uint32_t sentinel = 0;
};

static SimState build_state(
    const BuildResult& result,
    const std::vector<DeviceInstance>& devices
) {
    SimState sim;

    // Find unique signals
    std::set<uint32_t> unique;
    for (const auto& [port, sig] : result.port_to_signal) {
        unique.insert(sig);
    }

    // Remap signals to 0-based
    uint32_t next = 0;
    for (uint32_t old_sig : unique) {
        sim.signal_remap[old_sig] = next++;
    }
    sim.sentinel = next++;

    // Find fixed signals (RefNode v_out)
    std::set<uint32_t> fixed;
    for (const auto& dev : devices) {
        if (dev.classname == "RefNode") {
            float value = 0.0f;
            auto it_val = dev.params.find("value");
            if (it_val != dev.params.end()) value = std::stof(it_val->second);

            std::string port = dev.name + ".v";
            auto it_sig = result.port_to_signal.find(port);
            if (it_sig != result.port_to_signal.end()) {
                uint32_t new_sig = sim.signal_remap[it_sig->second];
                fixed.insert(new_sig);
                std::cout << "  FIXED: " << port << " = " << value << "V (signal " << new_sig << ")\n";
            }
        }
    }

    // Allocate signals
    for (size_t i = 0; i < next; ++i) {
        bool is_fixed = fixed.count(i) > 0;
        sim.state.allocate_signal(0.0f, {Domain::Electrical, is_fixed});
    }

    // Set fixed voltages
    for (const auto& dev : devices) {
        if (dev.classname == "RefNode") {
            float value = 0.0f;
            auto it_val = dev.params.find("value");
            if (it_val != dev.params.end()) value = std::stof(it_val->second);

            std::string port = dev.name + ".v";
            auto it_sig = result.port_to_signal.find(port);
            if (it_sig != result.port_to_signal.end()) {
                uint32_t new_sig = sim.signal_remap[it_sig->second];
                sim.state.across[new_sig] = value;
            }
        }
    }

    return sim;
}

static void run_simulation(SimState& sim, Systems& systems, int steps = 100) {
    const float omega = 1.5f;

    for (int step = 0; step < steps; ++step) {
        sim.state.clear_through();
        systems.solve_step(sim.state, step, 1.0f / 60.0f);
        sim.state.precompute_inv_conductance();

        for (size_t i = 0; i < sim.state.across.size(); ++i) {
            if (!sim.state.signal_types[i].is_fixed && sim.state.inv_conductance[i] > 0.0f) {
                sim.state.across[i] += sim.state.through[i] * sim.state.inv_conductance[i] * omega;
            }
        }

        // Post-step: relay switches copy voltage
        systems.post_step(sim.state, 1.0f / 60.0f);
    }
}

static void print_voltages(const SimState& sim) {
    std::cout << std::fixed << std::setprecision(2);
    for (size_t i = 0; i < sim.state.across.size(); ++i) {
        std::cout << "  signal[" << i << "]: " << sim.state.across[i] << "V";
        if (sim.state.signal_types[i].is_fixed) std::cout << " (FIXED)";
        std::cout << "\n";
    }
}

// =============================================================================
// Test 1: Battery + Load (battery drives the voltage)
// =============================================================================
static void test_battery_load() {
    std::cout << "\n=== TEST: Battery + Load ===\n";

    // Simple circuit: battery -> load -> ground
    // Battery internal R = 0.1 ohm, load R = 10 ohm
    // Expected voltage at load = 28 * 10 / (0.1 + 10) = 27.7V
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
        {"battery.v_out", "load.v_in"},   // battery + to load
        {"load.v_out", "gnd.v"},       // load to ground
        {"battery.v_in", "gnd.v"},     // battery - to ground
    };

    auto result = build_systems_dev(devices, conn);
    auto sim = build_state(result, devices);

    std::cout << "Running simulation...\n";
    run_simulation(sim, result.systems, 100);
    print_voltages(sim);

    // Expected: battery.v_out ≈ 27.7V, load.v_in ≈ 27.7V, gnd = 0V
    std::cout << "  (Expected: ~27.7V at load)\n";
}

// =============================================================================
// Test 2: Generator + Load
// =============================================================================
static void test_generator() {
    std::cout << "\n=== TEST: Generator + Load ===\n";

    // Simple circuit: generator -> load -> ground
    DeviceInstance gnd;
    gnd.name = "gnd";
    gnd.classname = "RefNode";
    gnd.params = {{"value", "0.0"}};
    gnd.ports = {{"v", {PortDirection::Out}}};

    DeviceInstance gen;
    gen.name = "generator";
    gen.classname = "Generator";
    gen.params = {{"v_nominal", "28.5"}, {"internal_r", "0.1"}};
    gen.ports = {{"v_in", {PortDirection::In}}, {"v_out", {PortDirection::Out}}};

    DeviceInstance load;
    load.name = "load";
    load.classname = "Resistor";
    load.params = {{"conductance", "0.1"}};
    load.ports = {{"v_in", {PortDirection::In}}, {"v_out", {PortDirection::Out}}};

    std::vector<DeviceInstance> devices = {gnd, gen, load};
    std::vector<std::pair<std::string, std::string>> conn = {
        {"generator.v_out", "load.v_in"},  // generator + to load
        {"load.v_out", "gnd.v"},        // load to ground
        {"generator.v_in", "gnd.v"},    // generator - to ground
    };

    auto result = build_systems_dev(devices, conn);
    auto sim = build_state(result, devices);

    std::cout << "Running simulation...\n";
    run_simulation(sim, result.systems, 100);
    print_voltages(sim);

    // Expected: generator.v_out ≈ 28.3V (28.5 - voltage drop)
}

// =============================================================================
// Test 3: Transformer
// =============================================================================
static void test_transformer() {
    std::cout << "\n=== TEST: Transformer ===\n";

    // Transformer: 100V primary -> 50V secondary (2:1 step down)
    DeviceInstance gnd;
    gnd.name = "gnd";
    gnd.classname = "RefNode";
    gnd.params = {{"value", "0.0"}};
    gnd.ports = {{"v", {PortDirection::Out}}};

    DeviceInstance source;
    source.name = "source";
    source.classname = "RefNode";
    source.params = {{"value", "100.0"}};
    source.ports = {{"v", {PortDirection::Out}}};

    DeviceInstance xfmr;
    xfmr.name = "xfmr";
    xfmr.classname = "Transformer";
    xfmr.params = {{"ratio", "0.5"}};
    xfmr.ports = {{"primary", {PortDirection::In}}, {"secondary", {PortDirection::Out}}};

    DeviceInstance load;
    load.name = "load";
    load.classname = "Resistor";
    load.params = {{"conductance", "0.1"}};
    load.ports = {{"v_in", {PortDirection::In}}, {"v_out", {PortDirection::Out}}};

    std::vector<DeviceInstance> devices = {gnd, source, xfmr, load};
    std::vector<std::pair<std::string, std::string>> conn = {
        {"source.v", "xfmr.primary"},  // source to primary
        {"xfmr.secondary", "load.v_in"},   // secondary to load
        {"load.v_out", "gnd.v"},        // load to ground
    };

    auto result = build_systems_dev(devices, conn);
    auto sim = build_state(result, devices);

    std::cout << "Running simulation...\n";
    run_simulation(sim, result.systems, 100);
    print_voltages(sim);
    std::cout << "  (Transformer is unstable - needs better solver)\n";
}

// =============================================================================
// Test 4: Indicator Light
// =============================================================================
static void test_indicator_light() {
    std::cout << "\n=== TEST: Indicator Light ===\n";

    DeviceInstance gnd{"gnd", "RefNode", {{"value", "0.0"}}, {{"v", "g"}}};
    DeviceInstance bus{"dc_bus", "RefNode", {{"value", "28.0"}}, {{"v", "bus"}}};
    DeviceInstance light{"light", "IndicatorLight", {{"max_brightness", "100.0"}}, {{"v_in", "p"}, {"v_out", "g"}, {"brightness", "b"}}};

    std::vector<DeviceInstance> devices = {gnd, bus, light};
    std::vector<std::pair<std::string, std::string>> conn = {
        {"dc_bus.v", "light.v_in"},
        {"light.v_out", "gnd.v"},
    };

    auto result = build_systems_dev(devices, conn);
    auto sim = build_state(result, devices);

    std::cout << "Running simulation...\n";
    run_simulation(sim, result.systems, 100);

    std::cout << std::fixed << std::setprecision(2);
    for (size_t i = 0; i < sim.state.across.size(); ++i) {
        std::cout << "  signal[" << i << "]: " << sim.state.across[i];
        if (sim.state.signal_types[i].is_fixed) std::cout << " (FIXED)";
        std::cout << "\n";
    }
}

// =============================================================================
// Test 5: Electric Pump (Hydraulic)
// =============================================================================
static void test_electric_pump() {
    std::cout << "\n=== TEST: Electric Pump ===\n";

    DeviceInstance gnd{"gnd", "RefNode", {{"value", "0.0"}}, {{"v", "g"}}};
    DeviceInstance bus{"dc_bus", "RefNode", {{"value", "28.0"}}, {{"v_in", "i"}, {"v_out", "o"}}};
    DeviceInstance pump{"pump", "ElectricPump", {{"max_pressure", "1000.0"}}, {{"v_in", "v"}, {"p_out", "p"}}};

    std::vector<DeviceInstance> devices = {gnd, bus, pump};
    std::vector<std::pair<std::string, std::string>> conn = {
        {"dc_bus.v_out", "pump.v_in"},
    };

    auto result = build_systems_dev(devices, conn);
    auto sim = build_state(result, devices);

    std::cout << "Running simulation...\n";
    run_simulation(sim, result.systems, 100);

    std::cout << std::fixed << std::setprecision(2);
    for (size_t i = 0; i < sim.state.across.size(); ++i) {
        std::cout << "  signal[" << i << "]: " << sim.state.across[i] << " (electrical: V, hydraulic: Pa)\n";
    }
    std::cout << "  (Expected: pressure ≈ 1000 Pa at 28V)\n";
}

// =============================================================================
// Test 6: Full DC Bus System (like real aircraft)
// =============================================================================
static void test_full_dc_bus() {
    std::cout << "\n=== TEST: Full DC Bus System ===\n";

    // Battery -> 2 Lights (each to ground)
    // Battery v_out = 28V, internal R = 0.1 ohm
    // Each light has conductance 0.35 (draws ~10W at 28V)
    // Total load conductance = 0.7
    // Expected: bus voltage = 28 * 0.7 / (0.1 + 0.7) = 26.5V
    DeviceInstance gnd{"gnd", "RefNode", {{"value", "0.0"}}, {{"v", "g"}}};
    DeviceInstance dc_bus{"dc_bus", "Bus", {}, {{"v", "bus"}}};
    DeviceInstance battery{"battery", "Battery", {{"v_nominal", "28.0"}, {"internal_r", "0.1"}}, {{"v_in", "i"}, {"v_out", "o"}}};
    DeviceInstance light1{"light1", "IndicatorLight", {{"max_brightness", "100.0"}}, {{"v_in", "p"}, {"v_out", "g"}, {"brightness", "b"}}};
    DeviceInstance light2{"light2", "IndicatorLight", {{"max_brightness", "100.0"}}, {{"v_in", "p"}, {"v_out", "g"}, {"brightness", "b"}}};

    std::vector<DeviceInstance> devices = {gnd, dc_bus, battery, light1, light2};

    std::vector<std::pair<std::string, std::string>> conn = {
        // Battery + to bus
        {"battery.v_out", "dc_bus.v"},
        // Bus to lights (high side)
        {"dc_bus.v", "light1.v_in"},
        {"dc_bus.v", "light2.v_in"},
        // Lights return to ground (low side)
        {"light1.v_out", "gnd.v"},
        {"light2.v_out", "gnd.v"},
        // Battery - to ground
        {"battery.v_in", "gnd.v"},
    };

    auto result = build_systems_dev(devices, conn);

    std::cout << "=== Port Mapping ===\n";
    for (const auto& [port, sig] : result.port_to_signal) {
        std::cout << port << " -> signal " << sig << "\n";
    }

    auto sim = build_state(result, devices);

    std::cout << "Components: " << result.systems.component_count() << "\n";
    std::cout << "Running simulation...\n";
    run_simulation(sim, result.systems, 100);
    print_voltages(sim);

    // Expected: bus voltage = 28 * 0.7 / (0.1 + 0.7) = 26.5V
    std::cout << "  (Expected: ~26.5V at bus with 2 lights)\n";
}

int main() {
    spdlog::set_level(spdlog::level::info);

    std::cout << "========================================\n";
    std::cout << "AN-24 C++ JIT Solver - Component Tests\n";
    std::cout << "========================================\n";

    test_battery_load();
    test_generator();
    test_transformer();
    test_indicator_light();
    test_electric_pump();
    test_full_dc_bus();

    std::cout << "\n========================================\n";
    std::cout << "All tests completed!\n";
    std::cout << "========================================\n";

    return 0;
}
