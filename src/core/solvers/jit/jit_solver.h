#pragma once

#include "core/solvers/common/port_registry.h"
#include "scheduler.h"
#include "subsolvers/subsolver_types.h"
#include "core/model/resolved_device.h"
#include "ui/core/interned_id.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declarations
struct SimulationState;
struct SolverOwnedRefs;

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
struct ElectricalPatchOp {
    ElectricalPatchKind kind = ElectricalPatchKind::AffineClamp;
    uint32_t element_id = UINT32_MAX;

    // Signal-driven operands (used by AffineClamp/LerpClamped01).
    uint32_t s0 = UINT32_MAX;
    uint32_t s1 = UINT32_MAX;
    uint32_t s2 = UINT32_MAX;
    uint32_t s3 = UINT32_MAX;
    uint32_t s4 = UINT32_MAX;

    // Pointer-driven state (used by BoolSwitch/IndexSwitch).
    const bool* bool_state = nullptr;
    const int* int_state = nullptr;
    int index_value = 0;

    // Constant outputs for switch kinds.
    float open_value = 0.0f;
    float closed_value = 0.0f;
};

/// Type-erased component step function signature.
using SolverStepFn = void (*)(void*, SimulationState&, double);

/// Compiled post-solve execute or commit operation for solver-owned components.
struct SolverStepOp {
    void* instance = nullptr;
    SolverStepFn fn = nullptr;
};

/// Typed port-to-signal mapping. Keys are interned "node_id.port_name" strings.
/// Runtime lookups are integer-only (InternedId comparison, no string hashing).
using PortToSignal = std::unordered_map<ui::InternedId, uint32_t>;

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

/// Build port-to-signal mapping from devices and connections
/// For AOT, this is used by codegen to generate component bindings
struct BuildResult {
    /// Special members defined in jit_solver.cpp where SolverOwnedRefs is complete.
    /// Required for unique_ptr<SolverOwnedRefs> with incomplete type (pimpl).
    ~BuildResult();
    BuildResult();
    BuildResult(BuildResult&&) noexcept;
    BuildResult& operator=(BuildResult&&) noexcept;
    BuildResult(const BuildResult&) = delete;
    BuildResult& operator=(const BuildResult&) = delete;

    uint32_t signal_count = 0;
    std::vector<uint32_t> fixed_signals;
    PortToSignal port_to_signal;

    /// Build-scoped interner for signal keys. Owns the string storage backing
    /// the InternedIds in port_to_signal. Destroyed on rebuild.
    ui::StringInterner signal_key_interner;

    /// Dynamic components for JIT mode (Editor).
    /// Storage: device name -> ComponentVariant (type-safe storage container).
    /// Mutable APIs are build-only; `build_systems_dev()` seals storage before
    /// return, preventing post-build mutation that could invalidate cached
    /// scheduler/solver pointers.
    BuildDeviceStore devices;

    /// Push scheduler populated at build time.
    PushScheduler scheduler;

    /// Electrical network build plan (for subsolver)
    ElectricalBuildPlan electrical_plan;

    /// LUT table arena - accumulated during build, moved to SimulationState at start
    std::vector<float> lut_keys;
    std::vector<float> lut_values;

    /// Pre-built typed pointer lists for solver-owned components.
    /// Eliminates per-frame std::visit over all 68+ variant types.
    /// Defined in jit_solver_detail.h; opaque here to avoid pulling
    /// 10 component headers into every consumer.
    std::unique_ptr<SolverOwnedRefs> solver_owned;

    /// Compiled pre-solve electrical patch operations.
    /// Each op writes current-frame mutable element values by element_id.
    std::vector<ElectricalPatchOp> electrical_patch_ops;

    /// Compiled post-solve execute operations for solver-owned components.
    /// Runs after solve_electrical so components can read branch currents.
    std::vector<SolverStepOp> solver_execute_ops;

    /// Compiled post-solve commit operations for solver-owned components.
    /// Eliminates per-frame per-type commit loops in simulator.
    std::vector<SolverStepOp> solver_commit_ops;
};

/// Pre-computed build input for the JIT solver.
/// Can be produced either from:
///   - elaborate_for_jit(FlatNetlist, ...)  — canonical BP2 path (no JSON)
///   - build_input_from_json(string)        — legacy JSON adapter (tests, CLI)
struct JitBuildInput {
    std::vector<ResolvedDevice> devices;
    std::vector<BridgePortDefinition> bridge_ports;
    PortToSignal port_to_signal;
    ui::StringInterner signal_key_interner;
    uint32_t signal_count = 0;
    std::unordered_map<std::string, float> initial_values;
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
