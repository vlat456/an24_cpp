#pragma once

#include "all.h"

// =============================================================================
// Explicit Template Instantiation for JitProvider
// Can be included after template definitions are visible (e.g., after all.cpp)
// =============================================================================

namespace an24 {

template class Battery<JitProvider>;
template class Switch<JitProvider>;
template class Relay<JitProvider>;
template class Resistor<JitProvider>;
template class Load<JitProvider>;
template class Comparator<JitProvider>;
template class HoldButton<JitProvider>;
template class Generator<JitProvider>;
template class GS24<JitProvider>;
template class Transformer<JitProvider>;
template class Inverter<JitProvider>;
template class LerpNode<JitProvider>;
template class PID<JitProvider>;
template class PD<JitProvider>;
template class PI<JitProvider>;
template class P<JitProvider>;
template class Splitter<JitProvider>;
template class Merger<JitProvider>;
template class IndicatorLight<JitProvider>;
template class Voltmeter<JitProvider>;
template class HighPowerLoad<JitProvider>;
template class ElectricPump<JitProvider>;
template class SolenoidValve<JitProvider>;
template class InertiaNode<JitProvider>;
template class TempSensor<JitProvider>;
template class ElectricHeater<JitProvider>;
template class Radiator<JitProvider>;
template class DMR400<JitProvider>;
template class RUG82<JitProvider>;
template class RU19A<JitProvider>;
template class Gyroscope<JitProvider>;
template class AGK47<JitProvider>;
template class Bus<JitProvider>;
template class BlueprintInput<JitProvider>;
template class BlueprintOutput<JitProvider>;
template class RefNode<JitProvider>;
template class VoltageSubtract<JitProvider>;
template class AND<JitProvider>;
template class OR<JitProvider>;
template class XOR<JitProvider>;
template class NOT<JitProvider>;
template class NAND<JitProvider>;
template class Any_V_to_Bool<JitProvider>;
template class Positive_V_to_Bool<JitProvider>;

} // namespace an24

