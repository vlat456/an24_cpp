#pragma once

/// Single source of truth for PatchOpKind → NodalPatchKind conversion.
/// Includes both enum definitions so the switch body compiles.
/// Used by JIT build_common.h and AOT electrical_codegen.cpp.

#include "core/solvers/common/nodal_patch_types.h"
#include "core/model/component_types.h"

/// Convert PatchOpKind (blueprint-level declaration) to NodalPatchKind (runtime).
/// Must be called with a non-None PatchOpKind (callers filter None before calling).
inline NodalPatchKind to_nodal_patch_kind(PatchOpKind k) {
    switch (k) {
        case PatchOpKind::AffineClamp:   return NodalPatchKind::AffineClamp;
        case PatchOpKind::LerpClamped01: return NodalPatchKind::LerpClamped01;
        case PatchOpKind::BoolSwitch:    return NodalPatchKind::BoolSwitch;
        case PatchOpKind::IndexSwitch:   return NodalPatchKind::IndexSwitch;
        case PatchOpKind::CopySignal:    return NodalPatchKind::CopySignal;
        case PatchOpKind::None:          break;  // unreachable — callers filter
    }
    assert(false && "to_nodal_patch_kind called with PatchOpKind::None");
    return NodalPatchKind::BoolSwitch;
}
