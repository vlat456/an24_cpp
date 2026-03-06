#pragma once

#include "state.h"
#include "component.h"
#include "systems.h"
#include <vector>
#include <unordered_map>
#include <string>
#include <memory>

namespace an24 {

/// Signal state for push-based solver (voltage only, current computed dynamically)
struct Signal {
    float voltage = 0.0f;
    float resistance = 0.0f;  // Resistance to ground from this point (for series circuits)
    // current вычисляется: I = ΔV / R
};

/// Simulation state for push-based solver
class PushState {
public:
    std::vector<Signal> signals;
    std::unordered_map<std::string, uint32_t> signal_to_idx;

    /// Get voltage at port
    float get_voltage(const std::string& port_ref) const {
        uint32_t idx = get_signal_idx(port_ref);
        return (idx < signals.size()) ? signals[idx].voltage : 0.0f;
    }

    /// Set voltage at port
    void set_voltage(const std::string& port_ref, float v) {
        uint32_t idx = get_signal_idx(port_ref);
        if (idx < signals.size()) {
            signals[idx].voltage = v;
        }
    }

    /// Get resistance at port
    float get_resistance(const std::string& port_ref) const {
        uint32_t idx = get_signal_idx(port_ref);
        return (idx < signals.size()) ? signals[idx].resistance : 0.0f;
    }

    /// Set resistance at port
    void set_resistance(const std::string& port_ref, float r) {
        uint32_t idx = get_signal_idx(port_ref);
        if (idx < signals.size()) {
            signals[idx].resistance = r;
        }
    }

    /// Get current through port (computed dynamically: I = ΔV / R)
    float get_current(const std::string& port_ref, float resistance) const {
        float v = get_voltage(port_ref);
        return (resistance > 0.0f) ? (v / resistance) : 0.0f;
    }

    /// Get effective resistance to ground from a port
    /// TODO: traverse component graph to compute total resistance
    float get_resistance_to_ground(const std::string& port_ref) const {
        // Simple implementation: assume 100Ω load if anything connected
        // TODO: implement proper graph traversal
        return 100.0f;
    }

    uint32_t get_signal_idx(const std::string& port_ref) const {
        auto it = signal_to_idx.find(port_ref);
        return (it != signal_to_idx.end()) ? it->second : UINT32_MAX;
    }

    Signal& get_signal(const std::string& port_ref) {
        uint32_t idx = get_signal_idx(port_ref);
        static Signal dummy;
        return (idx < signals.size()) ? signals[idx] : dummy;
    }

    const Signal& get_signal(const std::string& port_ref) const {
        uint32_t idx = get_signal_idx(port_ref);
        static Signal dummy;
        return (idx < signals.size()) ? signals[idx] : dummy;
    }
};

/// Push-based solver - direct voltage propagation without SOR
class PushSolver {
public:
    PushSolver() = default;
    ~PushSolver() = default;

    /// Build systems from devices and connections
    bool build(
        const std::vector<DeviceInstance>& devices,
        const std::vector<std::pair<std::string, std::string>>& connections
    );

    /// Run one simulation step (push voltages from sources)
    void step(PushState& state, float dt);

    /// Get port index by reference
    uint32_t get_port_idx(const std::string& port_ref) const {
        auto it = port_to_signal.find(port_ref);
        return (it != port_to_signal.end()) ? it->second : UINT32_MAX;
    }

private:
    Systems systems;
    std::unordered_map<std::string, uint32_t> port_to_signal;
    std::vector<std::string> signal_names;

    /// Connections for voltage propagation
    std::vector<std::pair<std::string, std::string>> connections;

    /// Topological sort for deterministic execution order
    std::vector<uint32_t> execution_order;
};

} // namespace an24
