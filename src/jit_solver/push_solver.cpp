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
        // TODO: parse ports from device
        // For now, just allocate signals based on component type
        if (dev.classname == "Battery") {
            port_to_signal[dev.name + ".v_in"] = signal_count++;
            port_to_signal[dev.name + ".v_out"] = signal_count++;
        } else if (dev.classname == "RefNode") {
            port_to_signal[dev.name + ".v"] = signal_count++;
        } else if (dev.classname == "Generator") {
            port_to_signal[dev.name + ".v_out"] = signal_count++;
            port_to_signal[dev.name + ".rpm"] = signal_count++;
        } else if (dev.classname == "IndicatorLight") {
            port_to_signal[dev.name + ".v_in"] = signal_count++;
            port_to_signal[dev.name + ".v_out"] = signal_count++;
            port_to_signal[dev.name + ".brightness"] = signal_count++;
        } else if (dev.classname == "Resistor") {
            port_to_signal[dev.name + ".v_in"] = signal_count++;
            port_to_signal[dev.name + ".v_out"] = signal_count++;
        } else if (dev.classname == "Wire") {
            port_to_signal[dev.name + ".v_in"] = signal_count++;
            port_to_signal[dev.name + ".v_out"] = signal_count++;
        } else if (dev.classname == "Switch" || dev.classname == "HoldButton") {
            port_to_signal[dev.name + ".v_in"] = signal_count++;
            port_to_signal[dev.name + ".v_out"] = signal_count++;
            port_to_signal[dev.name + ".control"] = signal_count++;
            port_to_signal[dev.name + ".state"] = signal_count++;
        }
    }

    // Create push-based components
    for (const auto& dev : devices) {
        uint32_t v_in = port_to_signal[dev.name + ".v_in"];
        uint32_t v_out = port_to_signal[dev.name + ".v_out"];
        uint32_t control = port_to_signal[dev.name + ".control"];
        uint32_t state = port_to_signal[dev.name + ".state"];

        std::unique_ptr<Component> comp;

        if (dev.classname == "Battery") {
            float v_nom = 28.0f;  // TODO: read from params
            float r_int = 0.01f;
            comp = std::make_unique<PushBattery>(v_in, v_out, v_nom, r_int);
        } else if (dev.classname == "RefNode") {
            float val = 0.0f;
            auto it = dev.params.find("value");
            if (it != dev.params.end()) {
                val = std::stof(it->second);
            }
            uint32_t v = port_to_signal[dev.name + ".v"];
            comp = std::make_unique<PushRefNode>(v, val);
        } else if (dev.classname == "Generator") {
            uint32_t v_out = port_to_signal[dev.name + ".v_out"];
            uint32_t rpm = port_to_signal[dev.name + ".rpm"];
            comp = std::make_unique<PushGenerator>(v_out, rpm);
        } else if (dev.classname == "Switch") {
            bool is_closed = false;  // TODO: read from params
            comp = std::make_unique<PushSwitch>(v_in, v_out, control, state, is_closed);
        } else if (dev.classname == "HoldButton") {
            comp = std::make_unique<PushHoldButton>(v_in, v_out, control, state);
        } else if (dev.classname == "IndicatorLight") {
            uint32_t brightness = port_to_signal[dev.name + ".brightness"];
            comp = std::make_unique<PushIndicatorLight>(v_in, v_out, brightness);
        } else if (dev.classname == "Resistor") {
            float r = 100.0f;
            auto it = dev.params.find("resistance");
            if (it != dev.params.end()) {
                r = std::stof(it->second);
            }
            comp = std::make_unique<PushResistor>(v_in, v_out, r);
        } else if (dev.classname == "Wire") {
            comp = std::make_unique<PushWire>(v_in, v_out);
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

        // Then components propagate resistance
        for (auto& comp : systems.electrical) {
            if (auto* res = dynamic_cast<PushResistor*>(comp.get())) {
                res->propagate_resistance(state);
            } else if (auto* light = dynamic_cast<PushIndicatorLight*>(comp.get())) {
                light->propagate_resistance(state);
            } else if (auto* wire = dynamic_cast<PushWire*>(comp.get())) {
                wire->propagate_resistance(state);
            }
        }
    }

    // Phase 1: Sources produce voltage (Battery, RefNode, Generator)
    for (auto& comp : systems.electrical) {
        if (auto* bat = dynamic_cast<PushBattery*>(comp.get())) {
            bat->push_voltage(state);
        } else if (auto* ref = dynamic_cast<PushRefNode*>(comp.get())) {
            ref->push_voltage(state);
        } else if (auto* gen = dynamic_cast<PushGenerator*>(comp.get())) {
            gen->push_voltage(state);
        }
    }

    // Phase 2: Update switch states
    for (auto& comp : systems.electrical) {
        if (auto* sw = dynamic_cast<PushSwitch*>(comp.get())) {
            sw->update_state(state);
        } else if (auto* btn = dynamic_cast<PushHoldButton*>(comp.get())) {
            btn->update_state(state);
        }
    }

    // Phase 3: Propagate voltages through connections
    for (const auto& conn : connections) {
        float v_from = state.get_voltage(conn.first);
        state.set_voltage(conn.second, v_from);
    }

    // Phase 4: Components propagate voltage (Switch, Light, Resistor, Wire)
    for (auto& comp : systems.electrical) {
        if (auto* sw = dynamic_cast<PushSwitch*>(comp.get())) {
            sw->push_voltage(state);
        } else if (auto* btn = dynamic_cast<PushHoldButton*>(comp.get())) {
            btn->push_voltage(state);
        } else if (auto* light = dynamic_cast<PushIndicatorLight*>(comp.get())) {
            light->push_voltage(state);
        } else if (auto* res = dynamic_cast<PushResistor*>(comp.get())) {
            res->push_voltage(state);
        } else if (auto* wire = dynamic_cast<PushWire*>(comp.get())) {
            wire->push_voltage(state);
        }
    }
}

} // namespace an24
