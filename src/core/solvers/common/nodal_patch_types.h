#pragma once

/// NodalPatchKind and NodalPatchOp — standalone types for pre-solve
/// patch operations. Zero dependencies beyond <cstdint>.
///
/// Used by:
///   - JIT simulator (via jit_solver.h)
///   - AOT codegen (emitted as constexpr data in generated headers)
///   - Shared update_nodal_dynamic_sources() (via nodal_patch_ops.h)
///
/// Adding a new kind requires updating:
///   1. This enum (and PatchOpKind in component_types.h)
///   2. to_nodal_patch_kind() in nodal_patch_convert.h
///   3. The switch in update_nodal_dynamic_sources() (nodal_patch_ops.h)

#include <cstdint>

/// Kind of pre-solve patch operation applied before each nodal solve.
/// Domain-agnostic: used by electrical, hydraulic, and pneumatic domains.
enum class NodalPatchKind : uint8_t {
    AffineClamp,    ///< cmd*gain+offset clamped to [min,max]
    LerpClamped01,  ///< lerp(lo, hi, clamp(cmd, 0, 1))
    BoolSwitch,     ///< signal > 0.5f → true/false value
    IndexSwitch,    ///< int(signal) == index → true/false value
    CopySignal      ///< copy signal value to element_value_a
};

/// Compiled pre-solve patch operation.
/// Each op writes current-frame mutable element values by element_id.
/// Fully signal-driven — all inputs read from st.values[]. No raw pointers.
struct NodalPatchOp {
    NodalPatchKind kind = NodalPatchKind::BoolSwitch;
    uint32_t element_id = UINT32_MAX;

    // Signal-driven operands (all kinds).
    // AffineClamp: s0=cmd, s1=gain, s2=offset, s3=min_v, s4=max_v
    // LerpClamped01: s0=cmd, s1=lo, s2=hi
    // BoolSwitch: s0=state_signal (>0.5f == true)
    // IndexSwitch: s0=position_signal (int cast, compared to index_value)
    // CopySignal: s0=source_signal
    uint32_t s0 = UINT32_MAX;
    uint32_t s1 = UINT32_MAX;
    uint32_t s2 = UINT32_MAX;
    uint32_t s3 = UINT32_MAX;
    uint32_t s4 = UINT32_MAX;

    // Integer constant for IndexSwitch (which position to match)
    int index_value = 0;

    // Constant outputs for switch kinds.
    // BoolSwitch: state_true_value = value when state_signal ≥ 0.5f
    //             state_false_value = value when state_signal < 0.5f
    // IndexSwitch: state_true_value = value when position matches index_value
    //              state_false_value = value otherwise
    float state_true_value = 0.0f;
    float state_false_value = 0.0f;
};