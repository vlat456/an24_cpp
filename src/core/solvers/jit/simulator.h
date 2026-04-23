#pragma once

#include "jit_solver.h"
#include "state.h"
#include "core/solvers/jit/subsolvers/electrical_subsolver.h"
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

/// Empty tag type for JIT solver specialization.
struct JIT_Solver {};

template <typename SolverTag>
class Simulator {
public:
    /// E-008: Maximum dt per step to prevent physics explosions.
    /// Frame hitches beyond this are clamped, not accumulated.
    static constexpr double MAX_DT = 0.1;

    Simulator() = default;
    ~Simulator() { stop(); }

    Simulator(const Simulator&) = delete;
    Simulator& operator=(const Simulator&) = delete;
    Simulator(Simulator&& other) noexcept;
    Simulator& operator=(Simulator&& other) noexcept;

    void start(const JitBuildInput& input);
    void stop();
    void step(double dt);

    bool is_running() const { return running_; }
    bool is_built() const { return build_result_.has_value(); }

    double get_time() const { return time_; }
    uint64_t get_step_count() const { return step_count_; }

    size_t get_signal_count() const { return state_.values.size(); }

    // === Typed signal API (zero allocation at steady state) ===

    /// Look up signal value by interned key. O(1) integer hash lookup.
    float get_signal_value(ui::InternedId key) const;

    /// Apply signal overrides by interned key.
    void apply_typed_overrides(const std::vector<std::pair<ui::InternedId, float>>& overrides);

    /// Access the build-scoped signal key interner.
    const ui::StringInterner& signal_key_interner() const;

    /// Resolve a (node_id, port_name) pair to an InternedId via the interner.
    /// Returns empty InternedId if not found. String construction is required
    /// here — use only at setup time, not in the hot path.
    ui::InternedId resolve_signal_key(std::string_view node_id, std::string_view port_name) const;

private:
    std::optional<BuildResult> build_result_;
    SimulationState state_;
    ElectricalRuntimeState electrical_rt_;

    bool running_ = false;
    double time_ = 0.0;
    uint64_t step_count_ = 0;
};

using JIT_Simulator = Simulator<JIT_Solver>;
