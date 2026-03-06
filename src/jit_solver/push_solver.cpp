#include "push_solver.h"
#include "components/push_components.h"
#include <algorithm>

namespace an24 {

bool PushSolver::build(
    const std::vector<DeviceInstance>& devices,
    const std::vector<std::pair<std::string, std::string>>& connections
) {
    // Clear previous state
    systems = Systems();
    port_to_signal.clear();
    signal_names.clear();
    execution_order.clear();

    // Store connections for propagation
    this->connections = connections;

    // Allocate signals for all ports
    uint32_t signal_count = 0;
    for (const auto& dev : devices) {
        // Allocate signals for all ports defined in device
        for (const auto& [port_name, port] : dev.ports) {
            (void)port;  // Direction not needed for allocation
            port_to_signal[dev.name + "." + port_name] = signal_count++;
        }
    }

    // Create push-based components
    for (const auto& dev : devices) {
        std::unique_ptr<Component> comp;

        if (dev.classname == "Battery") {
            uint32_t v_in = port_to_signal[dev.name + ".v_in"];
            uint32_t v_out = port_to_signal[dev.name + ".v_out"];
            float v_nom = 28.0f;  // TODO: read from params
            float r_int = 0.01f;
            comp = std::make_unique<PushBattery>(v_in, v_out, v_nom, r_int);
        } else if (dev.classname == "RefNode") {
            uint32_t v = port_to_signal[dev.name + ".v"];
            float val = 0.0f;
            auto it = dev.params.find("value");
            if (it != dev.params.end()) {
                val = std::stof(it->second);
            }
            comp = std::make_unique<PushRefNode>(v, val);
        } else if (dev.classname == "Generator") {
            uint32_t v_out = port_to_signal[dev.name + ".v_out"];
            uint32_t rpm = port_to_signal[dev.name + ".rpm"];
            comp = std::make_unique<PushGenerator>(v_out, rpm);
        } else if (dev.classname == "Switch") {
            uint32_t v_in = port_to_signal[dev.name + ".v_in"];
            uint32_t v_out = port_to_signal[dev.name + ".v_out"];
            uint32_t control = port_to_signal[dev.name + ".control"];
            uint32_t state = port_to_signal[dev.name + ".state"];
            bool is_closed = false;  // TODO: read from params
            comp = std::make_unique<PushSwitch>(v_in, v_out, control, state, is_closed);
        } else if (dev.classname == "HoldButton") {
            uint32_t v_in = port_to_signal[dev.name + ".v_in"];
            uint32_t v_out = port_to_signal[dev.name + ".v_out"];
            uint32_t control = port_to_signal[dev.name + ".control"];
            uint32_t state_port = port_to_signal[dev.name + ".state"];
            comp = std::make_unique<PushHoldButton>(v_in, v_out, control, state_port);
        } else if (dev.classname == "IndicatorLight") {
            uint32_t v_in = port_to_signal[dev.name + ".v_in"];
            uint32_t v_out = port_to_signal[dev.name + ".v_out"];
            uint32_t brightness = port_to_signal[dev.name + ".brightness"];
            comp = std::make_unique<PushIndicatorLight>(v_in, v_out, brightness);
        } else if (dev.classname == "Resistor") {
            uint32_t v_in = port_to_signal[dev.name + ".v_in"];
            uint32_t v_out = port_to_signal[dev.name + ".v_out"];
            float r = 100.0f;
            auto it = dev.params.find("resistance");
            if (it != dev.params.end()) {
                r = std::stof(it->second);
            }
            comp = std::make_unique<PushResistor>(v_in, v_out, r);
        } else if (dev.classname == "Wire") {
            uint32_t v_in = port_to_signal[dev.name + ".v_in"];
            uint32_t v_out = port_to_signal[dev.name + ".v_out"];
            comp = std::make_unique<PushWire>(v_in, v_out);
        } else if (dev.classname == "LerpNode") {
            uint32_t input = port_to_signal[dev.name + ".input"];
            uint32_t output = port_to_signal[dev.name + ".output"];
            float factor = 0.1f;
            auto it = dev.params.find("factor");
            if (it != dev.params.end()) {
                factor = std::stof(it->second);
            }
            comp = std::make_unique<PushLerpNode>(input, output, factor);
        } else if (dev.classname == "RU19A") {
            uint32_t v_start = port_to_signal[dev.name + ".v_start"];
            uint32_t v_out = port_to_signal[dev.name + ".v_out"];
            uint32_t k_mod = port_to_signal[dev.name + ".k_mod"];
            uint32_t rpm_out = port_to_signal[dev.name + ".rpm_out"];
            uint32_t t4_out = port_to_signal[dev.name + ".t4_out"];
            comp = std::make_unique<PushRU19A>(v_start, v_out, k_mod, rpm_out, t4_out);
        } else if (dev.classname == "RUG82") {
            uint32_t v_gen = port_to_signal[dev.name + ".v_gen"];
            uint32_t k_mod = port_to_signal[dev.name + ".k_mod"];
            comp = std::make_unique<PushRUG82>(v_gen, k_mod);
        } else if (dev.classname == "DMR400") {
            uint32_t v_gen_in = port_to_signal[dev.name + ".v_gen_in"];
            uint32_t v_out = port_to_signal[dev.name + ".v_out"];
            uint32_t v_bus_mon = port_to_signal[dev.name + ".v_bus_mon"];
            uint32_t lamp = port_to_signal[dev.name + ".lamp"];
            comp = std::make_unique<PushDMR400>(v_gen_in, v_out, v_bus_mon, lamp);
        } else if (dev.classname == "GS24") {
            uint32_t v_start = port_to_signal[dev.name + ".v_start"];
            uint32_t v_bus = port_to_signal[dev.name + ".v_bus"];
            uint32_t k_mod = port_to_signal[dev.name + ".k_mod"];
            uint32_t rpm_out = port_to_signal[dev.name + ".rpm_out"];
            comp = std::make_unique<PushGS24>(v_start, v_bus, k_mod, rpm_out);
        }

        if (comp) {
            systems.add_electrical(std::move(comp));
        }
    }

    // Simple topological sort: sources first, then components, then loads
    // For now, just use component order
    for (size_t i = 0; i < systems.electrical.size(); ++i) {
        execution_order.push_back(static_cast<uint32_t>(i));
    }

    return true;
}

void PushSolver::step(PushState& state, float dt) {
    // Initialize port mapping
    state.signal_to_idx = port_to_signal;
    state.signals.resize(port_to_signal.size());

    // Phase 0: Propagate resistance upstream (for series circuits)
    // This needs multiple iterations to propagate through all components
    for (int iter = 0; iter < 10; ++iter) {
        // First propagate through connections (reverse direction)
        for (const auto& conn : connections) {
            float r_to = state.get_resistance(conn.second);
            state.set_resistance(conn.first, r_to);
        }

        // Then components propagate resistance (Resistor, IndicatorLight, Wire)
        for (auto& comp : systems.electrical) {
            if ((comp->flags() & ComponentFlags::PropagatesResistance) != ComponentFlags::None) {
                comp->propagate_resistance(state);
            }
        }
    }

    // Phase 1: Sources produce voltage (Battery, RefNode, Generator, RU19A, GS24)
    for (auto& comp : systems.electrical) {
        if ((comp->flags() & ComponentFlags::VoltageSource) != ComponentFlags::None) {
            comp->push_voltage(state, dt);
        }
    }

    // Phase 2: Update switch states and state machines (Switch, HoldButton, GS24, RU19A)
    for (auto& comp : systems.electrical) {
        if ((comp->flags() & ComponentFlags::StateMachine) != ComponentFlags::None) {
            comp->update_state(state, dt);
        }
    }

    // Phase 3: Propagate voltages through connections
    for (const auto& conn : connections) {
        float v_from = state.get_voltage(conn.first);
        state.set_voltage(conn.second, v_from);
    }

    // Phase 4: Components propagate voltage (all push components)
    for (auto& comp : systems.electrical) {
        comp->push_voltage(state, dt);
    }
}

} // namespace an24
