#pragma once

#include <string>
#include <memory>

// Include shared types from json_parser
#include "../json_parser/json_parser.h"

namespace an24 {

/// Forward declaration
class SimulationState;

/// Component interface - base class for all devices
class Component {
public:
    virtual ~Component() = default;

    /// Component type name (for debugging)
    [[nodiscard]] virtual std::string_view type_name() const = 0;

    /// Solve electrical domain (every step, dt = frame delta)
    virtual void solve_electrical(SimulationState& state, float dt) {}

    /// Solve hydraulic domain (every 12th step, dt = 12 * frame delta)
    virtual void solve_hydraulic(SimulationState& state, float dt) {}

    /// Solve mechanical domain (every 3rd step, dt = 3 * frame delta)
    virtual void solve_mechanical(SimulationState& state, float dt) {}

    /// Solve thermal domain (every 60th step, dt = 60 * frame delta)
    virtual void solve_thermal(SimulationState& state, float dt) {}

    /// Solve logical domain (every step, dt = frame delta)
    virtual void solve_logical(SimulationState& state, float dt) {}

    /// Post-step update (once per frame, after SOR iteration)
    virtual void post_step(SimulationState& state, float dt) {}

    /// Pre-load initialization
    virtual void pre_load() {}
};

} // namespace an24

// ============================================================================
// PORTS Macro - Generate component port fields from registry
// ============================================================================
// Usage:
//   class RU19A : public Component {
//   public:
//       PORTS(RU19A, v_bus, v_start, k_mod, rpm_out, t4_out)
//       // Expands to:
//       // uint32_t v_bus_idx = 0;
//       // uint32_t v_start_idx = 0;
//       // uint32_t k_mod_idx = 0;
//       // uint32_t rpm_out_idx = 0;
//       // uint32_t t4_out_idx = 0;
//   };
//
// The macro generates uint32_t fields with _idx suffix for each port.
// This matches the port names in library/*.blueprint registry.
// Supports 1-32 ports.
// ============================================================================

#include <cstdint>

// Generate port fields from port name list (1-8 ports)
#define PORTS_1(Class, p1) uint32_t p1##_idx = 0;
#define PORTS_2(Class, p1, p2) uint32_t p1##_idx = 0; uint32_t p2##_idx = 0;
#define PORTS_3(Class, p1, p2, p3) uint32_t p1##_idx = 0; uint32_t p2##_idx = 0; uint32_t p3##_idx = 0;
#define PORTS_4(Class, p1, p2, p3, p4) uint32_t p1##_idx = 0; uint32_t p2##_idx = 0; uint32_t p3##_idx = 0; uint32_t p4##_idx = 0;
#define PORTS_5(Class, p1, p2, p3, p4, p5) uint32_t p1##_idx = 0; uint32_t p2##_idx = 0; uint32_t p3##_idx = 0; uint32_t p4##_idx = 0; uint32_t p5##_idx = 0;
#define PORTS_6(Class, p1, p2, p3, p4, p5, p6) uint32_t p1##_idx = 0; uint32_t p2##_idx = 0; uint32_t p3##_idx = 0; uint32_t p4##_idx = 0; uint32_t p5##_idx = 0; uint32_t p6##_idx = 0;
#define PORTS_7(Class, p1, p2, p3, p4, p5, p6, p7) uint32_t p1##_idx = 0; uint32_t p2##_idx = 0; uint32_t p3##_idx = 0; uint32_t p4##_idx = 0; uint32_t p5##_idx = 0; uint32_t p6##_idx = 0; uint32_t p7##_idx = 0;
#define PORTS_8(Class, p1, p2, p3, p4, p5, p6, p7, p8) uint32_t p1##_idx = 0; uint32_t p2##_idx = 0; uint32_t p3##_idx = 0; uint32_t p4##_idx = 0; uint32_t p5##_idx = 0; uint32_t p6##_idx = 0; uint32_t p7##_idx = 0; uint32_t p8##_idx = 0;

// 9-16 ports (compact format)
#define PORTS_9(Class, p1, p2, p3, p4, p5, p6, p7, p8, p9) \
    uint32_t p1##_idx = 0; uint32_t p2##_idx = 0; uint32_t p3##_idx = 0; uint32_t p4##_idx = 0; \
    uint32_t p5##_idx = 0; uint32_t p6##_idx = 0; uint32_t p7##_idx = 0; uint32_t p8##_idx = 0; \
    uint32_t p9##_idx = 0;

#define PORTS_10(Class, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10) \
    uint32_t p1##_idx = 0; uint32_t p2##_idx = 0; uint32_t p3##_idx = 0; uint32_t p4##_idx = 0; \
    uint32_t p5##_idx = 0; uint32_t p6##_idx = 0; uint32_t p7##_idx = 0; uint32_t p8##_idx = 0; \
    uint32_t p9##_idx = 0; uint32_t p10##_idx = 0;

#define PORTS_11(Class, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11) \
    uint32_t p1##_idx = 0; uint32_t p2##_idx = 0; uint32_t p3##_idx = 0; uint32_t p4##_idx = 0; \
    uint32_t p5##_idx = 0; uint32_t p6##_idx = 0; uint32_t p7##_idx = 0; uint32_t p8##_idx = 0; \
    uint32_t p9##_idx = 0; uint32_t p10##_idx = 0; uint32_t p11##_idx = 0;

#define PORTS_12(Class, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12) \
    uint32_t p1##_idx = 0; uint32_t p2##_idx = 0; uint32_t p3##_idx = 0; uint32_t p4##_idx = 0; \
    uint32_t p5##_idx = 0; uint32_t p6##_idx = 0; uint32_t p7##_idx = 0; uint32_t p8##_idx = 0; \
    uint32_t p9##_idx = 0; uint32_t p10##_idx = 0; uint32_t p11##_idx = 0; uint32_t p12##_idx = 0;

#define PORTS_13(Class, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13) \
    uint32_t p1##_idx = 0; uint32_t p2##_idx = 0; uint32_t p3##_idx = 0; uint32_t p4##_idx = 0; \
    uint32_t p5##_idx = 0; uint32_t p6##_idx = 0; uint32_t p7##_idx = 0; uint32_t p8##_idx = 0; \
    uint32_t p9##_idx = 0; uint32_t p10##_idx = 0; uint32_t p11##_idx = 0; uint32_t p12##_idx = 0; \
    uint32_t p13##_idx = 0;

#define PORTS_14(Class, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14) \
    uint32_t p1##_idx = 0; uint32_t p2##_idx = 0; uint32_t p3##_idx = 0; uint32_t p4##_idx = 0; \
    uint32_t p5##_idx = 0; uint32_t p6##_idx = 0; uint32_t p7##_idx = 0; uint32_t p8##_idx = 0; \
    uint32_t p9##_idx = 0; uint32_t p10##_idx = 0; uint32_t p11##_idx = 0; uint32_t p12##_idx = 0; \
    uint32_t p13##_idx = 0; uint32_t p14##_idx = 0;

#define PORTS_15(Class, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15) \
    uint32_t p1##_idx = 0; uint32_t p2##_idx = 0; uint32_t p3##_idx = 0; uint32_t p4##_idx = 0; \
    uint32_t p5##_idx = 0; uint32_t p6##_idx = 0; uint32_t p7##_idx = 0; uint32_t p8##_idx = 0; \
    uint32_t p9##_idx = 0; uint32_t p10##_idx = 0; uint32_t p11##_idx = 0; uint32_t p12##_idx = 0; \
    uint32_t p13##_idx = 0; uint32_t p14##_idx = 0; uint32_t p15##_idx = 0;

#define PORTS_16(Class, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16) \
    uint32_t p1##_idx = 0; uint32_t p2##_idx = 0; uint32_t p3##_idx = 0; uint32_t p4##_idx = 0; \
    uint32_t p5##_idx = 0; uint32_t p6##_idx = 0; uint32_t p7##_idx = 0; uint32_t p8##_idx = 0; \
    uint32_t p9##_idx = 0; uint32_t p10##_idx = 0; uint32_t p11##_idx = 0; uint32_t p12##_idx = 0; \
    uint32_t p13##_idx = 0; uint32_t p14##_idx = 0; uint32_t p15##_idx = 0; uint32_t p16##_idx = 0;

// 17-24 ports (compact format)
#define PORTS_17(Class, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17) \
    uint32_t p1##_idx = 0; uint32_t p2##_idx = 0; uint32_t p3##_idx = 0; uint32_t p4##_idx = 0; \
    uint32_t p5##_idx = 0; uint32_t p6##_idx = 0; uint32_t p7##_idx = 0; uint32_t p8##_idx = 0; \
    uint32_t p9##_idx = 0; uint32_t p10##_idx = 0; uint32_t p11##_idx = 0; uint32_t p12##_idx = 0; \
    uint32_t p13##_idx = 0; uint32_t p14##_idx = 0; uint32_t p15##_idx = 0; uint32_t p16##_idx = 0; \
    uint32_t p17##_idx = 0;

#define PORTS_18(Class, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18) \
    uint32_t p1##_idx = 0; uint32_t p2##_idx = 0; uint32_t p3##_idx = 0; uint32_t p4##_idx = 0; \
    uint32_t p5##_idx = 0; uint32_t p6##_idx = 0; uint32_t p7##_idx = 0; uint32_t p8##_idx = 0; \
    uint32_t p9##_idx = 0; uint32_t p10##_idx = 0; uint32_t p11##_idx = 0; uint32_t p12##_idx = 0; \
    uint32_t p13##_idx = 0; uint32_t p14##_idx = 0; uint32_t p15##_idx = 0; uint32_t p16##_idx = 0; \
    uint32_t p17##_idx = 0; uint32_t p18##_idx = 0;

#define PORTS_19(Class, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19) \
    uint32_t p1##_idx = 0; uint32_t p2##_idx = 0; uint32_t p3##_idx = 0; uint32_t p4##_idx = 0; \
    uint32_t p5##_idx = 0; uint32_t p6##_idx = 0; uint32_t p7##_idx = 0; uint32_t p8##_idx = 0; \
    uint32_t p9##_idx = 0; uint32_t p10##_idx = 0; uint32_t p11##_idx = 0; uint32_t p12##_idx = 0; \
    uint32_t p13##_idx = 0; uint32_t p14##_idx = 0; uint32_t p15##_idx = 0; uint32_t p16##_idx = 0; \
    uint32_t p17##_idx = 0; uint32_t p18##_idx = 0; uint32_t p19##_idx = 0;

#define PORTS_20(Class, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20) \
    uint32_t p1##_idx = 0; uint32_t p2##_idx = 0; uint32_t p3##_idx = 0; uint32_t p4##_idx = 0; \
    uint32_t p5##_idx = 0; uint32_t p6##_idx = 0; uint32_t p7##_idx = 0; uint32_t p8##_idx = 0; \
    uint32_t p9##_idx = 0; uint32_t p10##_idx = 0; uint32_t p11##_idx = 0; uint32_t p12##_idx = 0; \
    uint32_t p13##_idx = 0; uint32_t p14##_idx = 0; uint32_t p15##_idx = 0; uint32_t p16##_idx = 0; \
    uint32_t p17##_idx = 0; uint32_t p18##_idx = 0; uint32_t p19##_idx = 0; uint32_t p20##_idx = 0;

#define PORTS_21(Class, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21) \
    uint32_t p1##_idx = 0; uint32_t p2##_idx = 0; uint32_t p3##_idx = 0; uint32_t p4##_idx = 0; \
    uint32_t p5##_idx = 0; uint32_t p6##_idx = 0; uint32_t p7##_idx = 0; uint32_t p8##_idx = 0; \
    uint32_t p9##_idx = 0; uint32_t p10##_idx = 0; uint32_t p11##_idx = 0; uint32_t p12##_idx = 0; \
    uint32_t p13##_idx = 0; uint32_t p14##_idx = 0; uint32_t p15##_idx = 0; uint32_t p16##_idx = 0; \
    uint32_t p17##_idx = 0; uint32_t p18##_idx = 0; uint32_t p19##_idx = 0; uint32_t p20##_idx = 0; \
    uint32_t p21##_idx = 0;

#define PORTS_22(Class, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22) \
    uint32_t p1##_idx = 0; uint32_t p2##_idx = 0; uint32_t p3##_idx = 0; uint32_t p4##_idx = 0; \
    uint32_t p5##_idx = 0; uint32_t p6##_idx = 0; uint32_t p7##_idx = 0; uint32_t p8##_idx = 0; \
    uint32_t p9##_idx = 0; uint32_t p10##_idx = 0; uint32_t p11##_idx = 0; uint32_t p12##_idx = 0; \
    uint32_t p13##_idx = 0; uint32_t p14##_idx = 0; uint32_t p15##_idx = 0; uint32_t p16##_idx = 0; \
    uint32_t p17##_idx = 0; uint32_t p18##_idx = 0; uint32_t p19##_idx = 0; uint32_t p20##_idx = 0; \
    uint32_t p21##_idx = 0; uint32_t p22##_idx = 0;

#define PORTS_23(Class, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23) \
    uint32_t p1##_idx = 0; uint32_t p2##_idx = 0; uint32_t p3##_idx = 0; uint32_t p4##_idx = 0; \
    uint32_t p5##_idx = 0; uint32_t p6##_idx = 0; uint32_t p7##_idx = 0; uint32_t p8##_idx = 0; \
    uint32_t p9##_idx = 0; uint32_t p10##_idx = 0; uint32_t p11##_idx = 0; uint32_t p12##_idx = 0; \
    uint32_t p13##_idx = 0; uint32_t p14##_idx = 0; uint32_t p15##_idx = 0; uint32_t p16##_idx = 0; \
    uint32_t p17##_idx = 0; uint32_t p18##_idx = 0; uint32_t p19##_idx = 0; uint32_t p20##_idx = 0; \
    uint32_t p21##_idx = 0; uint32_t p22##_idx = 0; uint32_t p23##_idx = 0;

#define PORTS_24(Class, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24) \
    uint32_t p1##_idx = 0; uint32_t p2##_idx = 0; uint32_t p3##_idx = 0; uint32_t p4##_idx = 0; \
    uint32_t p5##_idx = 0; uint32_t p6##_idx = 0; uint32_t p7##_idx = 0; uint32_t p8##_idx = 0; \
    uint32_t p9##_idx = 0; uint32_t p10##_idx = 0; uint32_t p11##_idx = 0; uint32_t p12##_idx = 0; \
    uint32_t p13##_idx = 0; uint32_t p14##_idx = 0; uint32_t p15##_idx = 0; uint32_t p16##_idx = 0; \
    uint32_t p17##_idx = 0; uint32_t p18##_idx = 0; uint32_t p19##_idx = 0; uint32_t p20##_idx = 0; \
    uint32_t p21##_idx = 0; uint32_t p22##_idx = 0; uint32_t p23##_idx = 0; uint32_t p24##_idx = 0;

// 25-32 ports (compact format)
#define PORTS_25(Class, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25) \
    uint32_t p1##_idx = 0; uint32_t p2##_idx = 0; uint32_t p3##_idx = 0; uint32_t p4##_idx = 0; \
    uint32_t p5##_idx = 0; uint32_t p6##_idx = 0; uint32_t p7##_idx = 0; uint32_t p8##_idx = 0; \
    uint32_t p9##_idx = 0; uint32_t p10##_idx = 0; uint32_t p11##_idx = 0; uint32_t p12##_idx = 0; \
    uint32_t p13##_idx = 0; uint32_t p14##_idx = 0; uint32_t p15##_idx = 0; uint32_t p16##_idx = 0; \
    uint32_t p17##_idx = 0; uint32_t p18##_idx = 0; uint32_t p19##_idx = 0; uint32_t p20##_idx = 0; \
    uint32_t p25##_idx = 0;

#define PORTS_26(Class, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26) \
    uint32_t p1##_idx = 0; uint32_t p2##_idx = 0; uint32_t p3##_idx = 0; uint32_t p4##_idx = 0; \
    uint32_t p5##_idx = 0; uint32_t p6##_idx = 0; uint32_t p7##_idx = 0; uint32_t p8##_idx = 0; \
    uint32_t p9##_idx = 0; uint32_t p10##_idx = 0; uint32_t p11##_idx = 0; uint32_t p12##_idx = 0; \
    uint32_t p13##_idx = 0; uint32_t p14##_idx = 0; uint32_t p15##_idx = 0; uint32_t p16##_idx = 0; \
    uint32_t p17##_idx = 0; uint32_t p18##_idx = 0; uint32_t p19##_idx = 0; uint32_t p20##_idx = 0; \
    uint32_t p25##_idx = 0; uint32_t p26##_idx = 0;

#define PORTS_27(Class, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27) \
    uint32_t p1##_idx = 0; uint32_t p2##_idx = 0; uint32_t p3##_idx = 0; uint32_t p4##_idx = 0; \
    uint32_t p5##_idx = 0; uint32_t p6##_idx = 0; uint32_t p7##_idx = 0; uint32_t p8##_idx = 0; \
    uint32_t p9##_idx = 0; uint32_t p10##_idx = 0; uint32_t p11##_idx = 0; uint32_t p12##_idx = 0; \
    uint32_t p13##_idx = 0; uint32_t p14##_idx = 0; uint32_t p15##_idx = 0; uint32_t p16##_idx = 0; \
    uint32_t p17##_idx = 0; uint32_t p18##_idx = 0; uint32_t p19##_idx = 0; uint32_t p20##_idx = 0; \
    uint32_t p25##_idx = 0; uint32_t p26##_idx = 0; uint32_t p27##_idx = 0;

#define PORTS_28(Class, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28) \
    uint32_t p1##_idx = 0; uint32_t p2##_idx = 0; uint32_t p3##_idx = 0; uint32_t p4##_idx = 0; \
    uint32_t p5##_idx = 0; uint32_t p6##_idx = 0; uint32_t p7##_idx = 0; uint32_t p8##_idx = 0; \
    uint32_t p9##_idx = 0; uint32_t p10##_idx = 0; uint32_t p11##_idx = 0; uint32_t p12##_idx = 0; \
    uint32_t p13##_idx = 0; uint32_t p14##_idx = 0; uint32_t p15##_idx = 0; uint32_t p16##_idx = 0; \
    uint32_t p17##_idx = 0; uint32_t p18##_idx = 0; uint32_t p19##_idx = 0; uint32_t p20##_idx = 0; \
    uint32_t p25##_idx = 0; uint32_t p26##_idx = 0; uint32_t p27##_idx = 0; uint32_t p28##_idx = 0;

#define PORTS_29(Class, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29) \
    uint32_t p1##_idx = 0; uint32_t p2##_idx = 0; uint32_t p3##_idx = 0; uint32_t p4##_idx = 0; \
    uint32_t p5##_idx = 0; uint32_t p6##_idx = 0; uint32_t p7##_idx = 0; uint32_t p8##_idx = 0; \
    uint32_t p9##_idx = 0; uint32_t p10##_idx = 0; uint32_t p11##_idx = 0; uint32_t p12##_idx = 0; \
    uint32_t p13##_idx = 0; uint32_t p14##_idx = 0; uint32_t p15##_idx = 0; uint32_t p16##_idx = 0; \
    uint32_t p17##_idx = 0; uint32_t p18##_idx = 0; uint32_t p19##_idx = 0; uint32_t p20##_idx = 0; \
    uint32_t p25##_idx = 0; uint32_t p26##_idx = 0; uint32_t p27##_idx = 0; uint32_t p28##_idx = 0; \
    uint32_t p29##_idx = 0;

#define PORTS_30(Class, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30) \
    uint32_t p1##_idx = 0; uint32_t p2##_idx = 0; uint32_t p3##_idx = 0; uint32_t p4##_idx = 0; \
    uint32_t p5##_idx = 0; uint32_t p6##_idx = 0; uint32_t p7##_idx = 0; uint32_t p8##_idx = 0; \
    uint32_t p9##_idx = 0; uint32_t p10##_idx = 0; uint32_t p11##_idx = 0; uint32_t p12##_idx = 0; \
    uint32_t p13##_idx = 0; uint32_t p14##_idx = 0; uint32_t p15##_idx = 0; uint32_t p16##_idx = 0; \
    uint32_t p17##_idx = 0; uint32_t p18##_idx = 0; uint32_t p19##_idx = 0; uint32_t p20##_idx = 0; \
    uint32_t p25##_idx = 0; uint32_t p26##_idx = 0; uint32_t p27##_idx = 0; uint32_t p28##_idx = 0; \
    uint32_t p29##_idx = 0; uint32_t p30##_idx = 0;

#define PORTS_31(Class, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31) \
    uint32_t p1##_idx = 0; uint32_t p2##_idx = 0; uint32_t p3##_idx = 0; uint32_t p4##_idx = 0; \
    uint32_t p5##_idx = 0; uint32_t p6##_idx = 0; uint32_t p7##_idx = 0; uint32_t p8##_idx = 0; \
    uint32_t p9##_idx = 0; uint32_t p10##_idx = 0; uint32_t p11##_idx = 0; uint32_t p12##_idx = 0; \
    uint32_t p13##_idx = 0; uint32_t p14##_idx = 0; uint32_t p15##_idx = 0; uint32_t p16##_idx = 0; \
    uint32_t p17##_idx = 0; uint32_t p18##_idx = 0; uint32_t p19##_idx = 0; uint32_t p20##_idx = 0; \
    uint32_t p25##_idx = 0; uint32_t p26##_idx = 0; uint32_t p27##_idx = 0; uint32_t p28##_idx = 0; \
    uint32_t p29##_idx = 0; uint32_t p30_idx = 0; uint32_t p31##_idx = 0;

#define PORTS_32(Class, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32) \
    uint32_t p1##_idx = 0; uint32_t p2##_idx = 0; uint32_t p3##_idx = 0; uint32_t p4##_idx = 0; \
    uint32_t p5##_idx = 0; uint32_t p6##_idx = 0; uint32_t p7##_idx = 0; uint32_t p8##_idx = 0; \
    uint32_t p9##_idx = 0; uint32_t p10##_idx = 0; uint32_t p11##_idx = 0; uint32_t p12##_idx = 0; \
    uint32_t p13##_idx = 0; uint32_t p14##_idx = 0; uint32_t p15##_idx = 0; uint32_t p16##_idx = 0; \
    uint32_t p17##_idx = 0; uint32_t p18##_idx = 0; uint32_t p19##_idx = 0; uint32_t p20##_idx = 0; \
    uint32_t p25##_idx = 0; uint32_t p26##_idx = 0; uint32_t p27_idx = 0; uint32_t p28##_idx = 0; \
    uint32_t p29##_idx = 0; uint32_t p30_idx = 0; uint32_t p31##_idx = 0; uint32_t p32##_idx = 0;

// Dispatcher macro - select PORTS_N based on argument count
// Must match the number of PORTS_N macros (up to 32)
#define PORTS_GET_MACRO(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, NAME, ...) NAME

#define PORTS(Class, ...) \
    PORTS_GET_MACRO(__VA_ARGS__, PORTS_32, PORTS_31, PORTS_30, PORTS_29, PORTS_28, PORTS_27, PORTS_26, PORTS_25, PORTS_24, PORTS_23, PORTS_22, PORTS_21, PORTS_20, PORTS_19, PORTS_18, PORTS_17, PORTS_16, PORTS_15, PORTS_14, PORTS_13, PORTS_12, PORTS_11, PORTS_10, PORTS_9, PORTS_8, PORTS_7, PORTS_6, PORTS_5, PORTS_4, PORTS_3, PORTS_2, PORTS_1)(Class, __VA_ARGS__)
