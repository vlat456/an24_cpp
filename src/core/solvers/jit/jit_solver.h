#pragma once

#include "jit_build_input.h"
#include "core/solvers/common/port_registry.h"
#include "core/solvers/common/nodal_patch_types.h"
#include "scheduler.h"
#include "core/solvers/common/nodal_types.h"
#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declarations
struct SimulationState;

// ---- Compiled operation types (no component header dependencies) ----

// NodalPatchKind and NodalPatchOp are defined in nodal_patch_types.h
// (standalone header with zero dependencies beyond <cstdint>).

/// Compiled post-solve execute or commit operation for solver-owned components.
/// Reuses ErasedStep — type-safe encapsulated dispatch, zero overhead.
using SolverStepOp = ErasedStep;

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

    [[nodiscard]] size_t size() const { return devices_.size(); }
    [[nodiscard]] size_t count(const std::string& key) const { return devices_.count(key); }

    [[nodiscard]] const_iterator begin() const { return devices_.begin(); }
    [[nodiscard]] const_iterator end() const { return devices_.end(); }

    [[nodiscard]] const_iterator find(const std::string& key) const {
        return devices_.find(key);
    }

    [[nodiscard]] const_iterator cbegin() const { return devices_.cbegin(); }
    [[nodiscard]] const_iterator cend() const { return devices_.cend(); }

    [[nodiscard]] const ComponentVariant& at(const std::string& key) const {
        return devices_.at(key);
    }

    ComponentVariant* find_mutable(const std::string& key) {
        ensure_mutable("find_mutable");
        const auto it = devices_.find(key);
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
    [[nodiscard]] bool sealed() const { return sealed_; }

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
    return std::visit([]<typename T0>(T0& comp) -> Domain {
        using CompType = std::decay_t<T0>;
        return CompType::domain;
    }, variant);
}

/// Nodal-domain solver state — groups build artifacts + mutable runtime.
/// Used by all nodal domains (electrical, hydraulic, pneumatic).
/// Produced by domain-specific build pipelines at build time.
struct NodalArtifacts {
    // Build-time artifacts (immutable after build)
    NodalBuildPlan plan;
    std::vector<NodalPatchOp> patch_ops;
    std::vector<SolverStepOp> execute_ops;
    std::vector<SolverStepOp> commit_ops;

    // Per-frame mutable runtime state
    NodalRuntimeState runtime;
};

/// Lightweight view of one nodal domain slot — used by the simulator
/// for uniform iteration over all nodal domains without triplicating logic.
/// Does NOT own data; holds references into BuildResult and a pointer-to-member
/// for the corresponding SimulationState rt pointer.
struct NodalSlot {
    NodalArtifacts& artifacts;
    NodalRuntimeState* SimulationState::* rt_member;
};

/// Number of nodal solver domains (electrical, hydraulic, pneumatic).
/// Adding a 4th domain (e.g. Thermal) requires updating this constant,
/// adding a NodalArtifacts member, and adding a NodalSlot entry.
static constexpr size_t NODAL_DOMAIN_COUNT = 3;

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
    NodalArtifacts electrical;

    /// Hydraulic-domain build artifacts (plan, patch ops, step ops).
    NodalArtifacts hydraulic;

    /// Pneumatic-domain build artifacts (plan, patch ops, step ops).
    NodalArtifacts pneumatic;

    /// LUT table arena - accumulated during build, moved to SimulationState at start
    std::vector<float> lut_keys;
    std::vector<float> lut_values;

    /// Uniform iteration view for the simulator pipeline.
    /// Returned by value — NodalSlot holds references into *this.
    /// Caller must ensure BuildResult outlives the returned array.
    std::array<NodalSlot, NODAL_DOMAIN_COUNT> nodal_slots() noexcept {
        return {{
            { electrical, &SimulationState::electrical_rt },
            { hydraulic,  &SimulationState::hydraulic_rt  },
            { pneumatic,  &SimulationState::pneumatic_rt  },
        }};
    }
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
