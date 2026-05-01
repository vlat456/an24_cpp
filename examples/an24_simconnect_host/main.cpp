// =============================================================================
// AN-24 SimConnect Host — Minimal Example
// =============================================================================
//
// Demonstrates the full simulation + SimConnect bridge main loop.
// On macOS: uses stub client (simulates MSFS connection).
// On Windows: connects to real MSFS 2024 via SimConnect.
//
// Frame pipeline:
//   1. poll()          — Process SimConnect messages (CommBus responses)
//   2. read_into()     — Buffered MSFS values → simulator signals
//   3. step(dt)        — Run simulation
//   4. write_from()    — Simulator signals → CommBus set_var requests

#include "core/solvers/jit/bridge/simvar_provider_host.h"
#include "core/solvers/jit/simulator.h"
#include "core/solvers/jit/jit_build_input.h"
#include "io/json/parse_json_api.h"
#include "simconnect/simconnect_provider.h"

#include <spdlog/spdlog.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

static std::atomic<bool> g_running{true};

static void signal_handler(int /*signum*/) {
    g_running.store(false, std::memory_order_relaxed);
}

int main() {
    std::signal(SIGINT, signal_handler);

    spdlog::info("=== AN-24 SimConnect Host ===");

    // Register provider types
    SimConnectProvider::register_type();

    // 1. Load blueprint and start simulator
    // For this example, we create a minimal build input programmatically.
    // In production, load from JSON: auto input = parse_json("an24_systems.blueprint");
    JitBuildInput input;

    // SimConnectInput: reads ambient temperature from MSFS
    {
        SolverDevice dev;
        dev.name = "msfs_ambient_temp";
        dev.classname = "SimConnectInput";
        dev.kind = ComponentKind::SimConnectInput;
        dev.scheduler_role_kind = SchedulerRoleKind::Source;
        dev.params["var_name"] = "AMBIENT TEMPERATURE";
        dev.params["var_type"] = "AVar";
        dev.params["unit"] = "Celsius";
        dev.params["default_value"] = "15.0";
        dev.ports["out"] = Port{bp2::Direction::Output, PortType::Signal, Domain::Logical, true};
        input.devices.push_back(std::move(dev));
    }

    // SimConnectOutput: writes bus voltage to MSFS
    {
        SolverDevice dev;
        dev.name = "msfs_bus_voltage";
        dev.classname = "SimConnectOutput";
        dev.kind = ComponentKind::SimConnectOutput;
        dev.scheduler_role_kind = SchedulerRoleKind::Consumer;
        dev.params["var_name"] = "ELECTRICAL MAIN BUS VOLTAGE";
        dev.params["var_type"] = "AVar";
        dev.params["unit"] = "Volts";
        dev.params["mode"] = "data";
        dev.ports["in"] = Port{bp2::Direction::Input, PortType::Signal, Domain::Logical, false};
        input.devices.push_back(std::move(dev));
    }

    // Set up signal mapping
    input.signal_key_interner.intern("msfs_ambient_temp.out");
    input.signal_key_interner.intern("msfs_bus_voltage.in");
    input.port_to_signal[input.signal_key_interner.intern("msfs_ambient_temp.out")] = 0;
    input.port_to_signal[input.signal_key_interner.intern("msfs_bus_voltage.in")] = 1;
    input.signal_count = 2;
    input.initial_values["msfs_ambient_temp.out"] = 15.0f;
    input.initial_values["msfs_bus_voltage.in"] = 0.0f;

    // Start simulator
    JIT_Simulator sim;
    sim.start(input);
    spdlog::info("Simulator started: {} signals", sim.get_signal_count());

    // 2. Create and configure provider host
    SimvarProviderHost host;
    host.build(input, sim);
    spdlog::info("Provider host built: {} provider(s)", host.provider_count());

    // 3. Connect to SimConnect (stub on macOS, real on Windows)
    if (!host.connect()) {
        spdlog::error("Failed to connect to SimConnect");
        return 1;
    }
    spdlog::info("Connected to SimConnect");

    // 4. Main simulation loop
    auto last_time = std::chrono::steady_clock::now();
    uint64_t frame_count = 0;

    spdlog::info("Running simulation loop (Ctrl+C to stop)...");

    const uint32_t signal_count = static_cast<uint32_t>(sim.get_signal_count());

    while (g_running.load(std::memory_order_relaxed)) {
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - last_time).count();
        last_time = now;

        // Clamp dt to prevent physics explosions
        dt = std::min(dt, JIT_Simulator::MAX_DT);

        // Frame pipeline:
        // 1. Process SimConnect messages (CommBus responses from WASM bridge)
        host.poll(dt);

        // 2. Read external inputs directly into simulator's values array
        host.read_into(sim.values(), signal_count);

        // 3. Run simulation step
        sim.step(dt);

        // 4. Write simulator outputs directly from values array to external source
        host.write_from(sim.values(), signal_count);

        frame_count++;

        // Print status every 60 frames (~1 second at 60 Hz)
        if (frame_count % 60 == 0) {
            spdlog::info("Frame {} | time={:.2f}s", frame_count, sim.get_time());
        }

        // Target ~60 Hz frame rate
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    spdlog::info("Shutting down after {} frames", frame_count);
    host.disconnect();
    sim.stop();

    return 0;
}
