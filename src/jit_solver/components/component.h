#pragma once

#include <cstdint>

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
// This matches the port names in components/*.json registry.
// ============================================================================

// Generate port fields from port name list
#define PORTS_1(Class, p1) \
    uint32_t p1##_idx = 0;

#define PORTS_2(Class, p1, p2) \
    uint32_t p1##_idx = 0; \
    uint32_t p2##_idx = 0;

#define PORTS_3(Class, p1, p2, p3) \
    uint32_t p1##_idx = 0; \
    uint32_t p2##_idx = 0; \
    uint32_t p3##_idx = 0;

#define PORTS_4(Class, p1, p2, p3, p4) \
    uint32_t p1##_idx = 0; \
    uint32_t p2##_idx = 0; \
    uint32_t p3##_idx = 0; \
    uint32_t p4##_idx = 0;

#define PORTS_5(Class, p1, p2, p3, p4, p5) \
    uint32_t p1##_idx = 0; \
    uint32_t p2##_idx = 0; \
    uint32_t p3##_idx = 0; \
    uint32_t p4##_idx = 0; \
    uint32_t p5##_idx = 0;

#define PORTS_6(Class, p1, p2, p3, p4, p5, p6) \
    uint32_t p1##_idx = 0; \
    uint32_t p2##_idx = 0; \
    uint32_t p3##_idx = 0; \
    uint32_t p4##_idx = 0; \
    uint32_t p5##_idx = 0; \
    uint32_t p6##_idx = 0;

#define PORTS_7(Class, p1, p2, p3, p4, p5, p6, p7) \
    uint32_t p1##_idx = 0; \
    uint32_t p2##_idx = 0; \
    uint32_t p3##_idx = 0; \
    uint32_t p4##_idx = 0; \
    uint32_t p5##_idx = 0; \
    uint32_t p6##_idx = 0; \
    uint32_t p7##_idx = 0;

#define PORTS_8(Class, p1, p2, p3, p4, p5, p6, p7, p8) \
    uint32_t p1##_idx = 0; \
    uint32_t p2##_idx = 0; \
    uint32_t p3##_idx = 0; \
    uint32_t p4##_idx = 0; \
    uint32_t p5##_idx = 0; \
    uint32_t p6##_idx = 0; \
    uint32_t p7##_idx = 0; \
    uint32_t p8##_idx = 0;

// Dispatcher macro - select PORTS_N based on argument count
#define PORTS_GET_MACRO(_1, _2, _3, _4, _5, _6, _7, _8, NAME, ...) NAME

#define PORTS(Class, ...) \
    PORTS_GET_MACRO(__VA_ARGS__, PORTS_8, PORTS_7, PORTS_6, PORTS_5, PORTS_4, PORTS_3, PORTS_2, PORTS_1)(Class, __VA_ARGS__)
