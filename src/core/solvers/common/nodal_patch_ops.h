#pragma once

/// Pre-solve pass: apply compiled nodal patch operations.
/// Domain-agnostic: works for electrical, hydraulic, or any nodal domain.
/// Reads all inputs from st.values[] — fully signal-driven, no component
/// member field access. This is the single source of truth for dynamic
/// source patching, shared between JIT and AOT paths.

#include "core/solvers/jit/jit_solver.h"  // NodalPatchOp, NodalPatchKind
#include "core/solvers/jit/state.h"       // SimulationState
#include "core/solvers/jit/subsolvers/nodal_types.h"  // NodalRuntimeState

#include <algorithm>
#include <cstdint>
#include <vector>

/// Apply patch ops from a pointer+count array (for AOT constexpr arrays).
inline void update_nodal_dynamic_sources(
    const NodalPatchOp* ops,
    size_t op_count,
    SimulationState& st,
    NodalRuntimeState& rt)
{
    const uint32_t signal_count = static_cast<uint32_t>(st.values.size());

    for (size_t i = 0; i < op_count; ++i) {
        const auto& op = ops[i];
        if (op.element_id >= rt.element_value_a.size()) {
            continue;
        }

        // Guard: all signal operands must be in-bounds. Prevents UB if a
        // port mapping is missing (JitProvider returns UNMAPPED = UINT32_MAX
        // for unmapped ports — debug asserts, but release would be OOB).
        if (op.s0 >= signal_count) continue;

        float out = rt.element_value_a[op.element_id];
        switch (op.kind) {
            case NodalPatchKind::AffineClamp: {
                if (op.s1 >= signal_count || op.s2 >= signal_count ||
                    op.s3 >= signal_count || op.s4 >= signal_count) continue;
                float cmd = st.values[op.s0];
                float gain = st.values[op.s1];
                float offset = st.values[op.s2];
                float min_v = st.values[op.s3];
                float max_v = st.values[op.s4];
                out = std::clamp(cmd * gain + offset, min_v, max_v);
                break;
            }
            case NodalPatchKind::LerpClamped01: {
                if (op.s1 >= signal_count || op.s2 >= signal_count) continue;
                float cmd = st.values[op.s0];
                float lo = st.values[op.s1];
                float hi = st.values[op.s2];
                float t = std::clamp(cmd, 0.0f, 1.0f);
                out = lo + (hi - lo) * t;
                break;
            }
            case NodalPatchKind::BoolSwitch: {
                const bool state = st.values[op.s0] > 0.5f;
                out = state ? op.state_true_value : op.state_false_value;
                break;
            }
            case NodalPatchKind::IndexSwitch: {
                const int idx = static_cast<int>(st.values[op.s0]);
                out = (idx == op.index_value) ? op.state_true_value : op.state_false_value;
                break;
            }
            case NodalPatchKind::CopySignal: {
                out = st.values[op.s0];
                break;
            }
        }
        rt.element_value_a[op.element_id] = out;
    }
}

/// Overload accepting std::vector (convenience for JIT).
inline void update_nodal_dynamic_sources(
    const std::vector<NodalPatchOp>& ops,
    SimulationState& st,
    NodalRuntimeState& rt)
{
    update_nodal_dynamic_sources(ops.data(), ops.size(), st, rt);
}