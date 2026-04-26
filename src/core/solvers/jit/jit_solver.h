#pragma once

#include "jit_build_input.h"
#include "core/solvers/common/port_registry.h"
#include "scheduler.h"
#include "subsolvers/subsolver_types.h"
#include "subsolvers/hydraulic_subsolver_types.h"
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declarations
struct SimulationState;

// ---- Compiled operation types (no component header dependencies) ----

/// Kind of electrical patch operation applied before each solve.
enum class ElectricalPatchKind : uint8_t {
    AffineClamp,
    LerpClamped01,
    BoolSwitch,
    IndexSwitch
};

/// Compiled pre-solve electrical patch operation.
/// Each op writes current-frame mutable element values by element_id.
/// Fully signal-driven — all inputs read from st.values[]. No raw pointers.
struct ElectricalPatchOp {
    ElectricalPatchKind kind = ElectricalPatchKind::AffineClamp;
    uint32_t element_id = UINT32_MAX;

    // Signal-driven operands (all kinds).
    // AffineClamp: s0=cmd, s1=gain, s2=offset, s3=min_v, s4=max_v
    // LerpClamped01: s0=cmd, s1=lo, s2=hi
    // BoolSwitch: s0=state_signal (>0.5f == true)
    // IndexSwitch: s0=position_signal (int cast, compared to index_value)
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

/// Type-erased component step function signature.
using SolverStepFn = void (*)(void*, SimulationState&, double);

/// Compiled post-solve execute or commit operation for solver-owned components.
struct SolverStepOp {
    void* instance = nullptr;
    SolverStepFn fn = nullptr;
};

/// Guarded component storage.
///
/// Build-time code uses mutable APIs (`operator[]`, `find_mutable`,
/// `for_each_mutable`) to populate components and wire typed pointers.
/// Once `seal()` is called, all mutable APIs throw, so post-build structural
/// mutation cannot silently invalidate cached scheduler/solver pointers.
class BuildDeviceStore {
public:
    using Storage = std::unordered_map<std::string, ComponentVariant>;
    using const_iterator = Storage::const_iterator;

    ComponentVariant& operator[](const std::string& key) {
        ensure_mutable("operator[]");
        return devices_[key];
    }

    size_t size() const { return devices_.size(); }
    size_t count(const std::string& key) const { return devices_.count(key); }

    const_iterator begin() const { return devices_.begin(); }
    const_iterator end() const { return devices_.end(); }

    const_iterator find(const std::string& key) const {
        return devices_.find(key);
    }

    const_iterator cbegin() const { return devices_.cbegin(); }
    const_iterator cend() const { return devices_.cend(); }

    const ComponentVariant& at(const std::string& key) const {
        return devices_.at(key);
    }

    ComponentVariant* find_mutable(const std::string& key) {
        ensure_mutable("find_mutable");
        auto it = devices_.find(key);
        if (it == devices_.end()) {
            return nullptr;
        }
        return &it->second;
    }

    template <typename Fn>
    void for_each_mutable(Fn&& fn) {
        ensure_mutable("for_each_mutable");
        for (auto& [name, variant] : devices_) {
            fn(name, variant);
        }
    }

    void reserve(size_t n) { devices_.reserve(n); }
    void seal() { sealed_ = true; }
    bool sealed() const { return sealed_; }

private:
    void ensure_mutable(const char* op) const {
        if (sealed_) {
            throw std::logic_error(
                std::string("BuildDeviceStore is sealed; mutable operation '") + op +
                "' is not allowed after build");
        }
    }

    Storage devices_;
    bool sealed_ = false;
};

/// Get domain bitmask from component (reads static constexpr Domain field)
inline Domain get_component_domain_mask(const ComponentVariant& variant) {
    return std::visit([](auto& comp) -> Domain {
        using CompType = std::decay_t<decltype(comp)>;
        return CompType::domain;
    }, variant);
}

/// Electrical-domain solver state — groups build artifacts + mutable runtime.
/// Produced by build_electrical.cpp at build time. Runtime state is mutated
/// each frame by the simulator.
struct ElectricalArtifacts {
    // Build-time artifacts (immutable after build)
    ElectricalBuildPlan plan;
    std::vector<ElectricalPatchOp> patch_ops;
    std::vector<SolverStepOp> execute_ops;
    std::vector<SolverStepOp> commit_ops;

    // Per-frame mutable runtime state
    ElectricalRuntimeState runtime;
};

// ---- Hydraulic patch operation types ----

/// Kind of hydraulic patch operation applied before each solve.
enum class HydraulicPatchKind : uint8_t {
    BoolSwitch,     ///< Switch conductance based on state signal (SolenoidValve)
    CopySignal      ///< Copy a signal value to element_value_a (FuelTank pressure)
};

/// Compiled pre-solve hydraulic patch operation.
/// Fully signal-driven — all inputs read from st.values[]. No raw pointers.
struct HydraulicPatchOp {
    HydraulicPatchKind kind = HydraulicPatchKind::BoolSwitch;
    uint32_t element_id = UINT32_MAX;

    // Signal-driven operands.
    // BoolSwitch: s0=state_signal (>0.5f == true)
    // CopySignal: s0=source_signal (pressure value to copy)
    uint32_t s0 = UINT32_MAX;

    // Constant outputs for BoolSwitch.
    // state_true_value = value when state_signal ≥ 0.5f
    // state_false_value = value when state_signal < 0.5f
    float state_true_value = 0.0f;
    float state_false_value = 0.0f;
};

/// Hydraulic-domain solver state — groups build artifacts + mutable runtime.
/// Produced by build_hydraulic.cpp at build time. Runtime state is mutated
/// each frame by the simulator.
struct HydraulicArtifacts {
    // Build-time artifacts (immutable after build)
    HydraulicBuildPlan plan;
    std::vector<HydraulicPatchOp> patch_ops;
    std::vector<SolverStepOp> execute_ops;
    std::vector<SolverStepOp> commit_ops;

    // Per-frame mutable runtime state
    HydraulicRuntimeState runtime;
};

/// Build port-to-signal mapping from devices and connections
/// For AOT, this is used by codegen to generate component bindings
struct BuildResult {
    BuildResult() = default;
    ~BuildResult() = default;
    BuildResult(BuildResult&&) noexcept = default;
    BuildResult& operator=(BuildResult&&) noexcept = default;
    BuildResult(const BuildResult&) = delete;
    BuildResult& operator=(const BuildResult&) = delete;

    uint32_t signal_count = 0;
    std::vector<uint32_t> fixed_signals;
    PortToSignal port_to_signal;

    /// Build-scoped interner for signal keys. Owns the string storage backing
    /// the InternedIds in port_to_signal. Destroyed on rebuild.
    core::StringInterner signal_key_interner;

    /// Dynamic components for JIT mode (Editor).
    /// Storage: device name -> ComponentVariant (type-safe storage container).
    /// Mutable APIs are build-only; `build_systems_dev()` seals storage before
    /// return, preventing post-build mutation that could invalidate cached
    /// scheduler/solver pointers.
    BuildDeviceStore devices;

    /// Push scheduler populated at build time.
    PushScheduler scheduler;

    /// Electrical-domain build artifacts (plan, patch ops, step ops).
    ElectricalArtifacts electrical;

    /// Hydraulic-domain build artifacts (plan, patch ops, step ops).
    HydraulicArtifacts hydraulic;

    /// LUT table arena - accumulated during build, moved to SimulationState at start
    std::vector<float> lut_keys;
    std::vector<float> lut_values;
};

/// Build solver runtime from pre-computed input (canonical path).
/// port_to_signal and signal_count are taken directly from the input;
/// connections/UnionFind are NOT needed.
BuildResult build_systems_dev(const JitBuildInput& input);

/// Convert JSON string to JitBuildInput for canonical runtime path.
/// Parses JSON, computes port_to_signal mapping via UnionFind,
/// then returns JitBuildInput ready for build_systems_dev(JitBuildInput) or Simulator::start().
/// This is the adapter for JSON-based tests transitioning to canonical APIs.
JitBuildInput build_input_from_json(const std::string& json_str);
