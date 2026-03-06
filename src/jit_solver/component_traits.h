#pragma once

namespace an24 {

// Forward declarations
class PushBattery;
class PushRefNode;
class PushGenerator;
class PushSwitch;
class PushHoldButton;
class PushIndicatorLight;
class PushResistor;
class PushWire;
class PushLerpNode;
class PushGS24;
class PushRUG82;
class PushDMR400;
class PushRU19A;

/// Component category traits (compile-time)
template<typename T>
struct ComponentTraits {
    static constexpr bool is_voltage_source = false;
    static constexpr bool has_state_machine = false;
    static constexpr bool propagates_resistance = false;
};

// Voltage sources (Phase 1)
template<>
struct ComponentTraits<PushBattery> {
    static constexpr bool is_voltage_source = true;
    static constexpr bool has_state_machine = false;
    static constexpr bool propagates_resistance = false;
};

template<>
struct ComponentTraits<PushRefNode> {
    static constexpr bool is_voltage_source = true;
    static constexpr bool has_state_machine = false;
    static constexpr bool propagates_resistance = false;
};

template<>
struct ComponentTraits<PushGenerator> {
    static constexpr bool is_voltage_source = true;
    static constexpr bool has_state_machine = false;
    static constexpr bool propagates_resistance = false;
};

// State machines (Phase 2)
template<>
struct ComponentTraits<PushSwitch> {
    static constexpr bool is_voltage_source = false;
    static constexpr bool has_state_machine = true;
    static constexpr bool propagates_resistance = false;
};

template<>
struct ComponentTraits<PushHoldButton> {
    static constexpr bool is_voltage_source = false;
    static constexpr bool has_state_machine = true;
    static constexpr bool propagates_resistance = false;
};

template<>
struct ComponentTraits<PushGS24> {
    static constexpr bool is_voltage_source = false;
    static constexpr bool has_state_machine = true;
    static constexpr bool propagates_resistance = false;
};

template<>
struct ComponentTraits<PushRU19A> {
    static constexpr bool is_voltage_source = false;
    static constexpr bool has_state_machine = true;
    static constexpr bool propagates_resistance = false;
};

// Resistance propagators (Phase 0)
template<>
struct ComponentTraits<PushIndicatorLight> {
    static constexpr bool is_voltage_source = false;
    static constexpr bool has_state_machine = false;
    static constexpr bool propagates_resistance = true;
};

template<>
struct ComponentTraits<PushResistor> {
    static constexpr bool is_voltage_source = false;
    static constexpr bool has_state_machine = false;
    static constexpr bool propagates_resistance = true;
};

template<>
struct ComponentTraits<PushWire> {
    static constexpr bool is_voltage_source = false;
    static constexpr bool has_state_machine = false;
    static constexpr bool propagates_resistance = true;
};

} // namespace an24
