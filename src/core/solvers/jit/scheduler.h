#pragma once

#include "erased_step.h"

#include <cassert>
#include <cstddef>
#include <vector>

struct SimulationState;

/// A scheduled component with separate execute and commit phases.
/// Constructed exclusively via ScheduledComponent::for_type<T>(T*),
/// which captures the concrete type in two ErasedStep primitives.
struct ScheduledComponent {
    /// Execute the component's per-frame computation.
    void execute(SimulationState& st, double dt) const {
        execute_step_.invoke(st, dt);
    }

    /// Commit state transitions (one-frame delay semantics).
    /// No-op if the component has no commit method.
    void commit(SimulationState& st, double dt) const {
        if (commit_step_) {
            commit_step_.invoke(st, dt);
        }
    }

    /// Only way to construct — type T is captured in both ErasedSteps.
    template <typename T>
    static ScheduledComponent for_type(T* component) {
        return ScheduledComponent{
            ErasedStep::execute_for(component),
            ErasedStep::commit_for(component)
        };
    }

private:
    ScheduledComponent(ErasedStep exec, ErasedStep commit)
        : execute_step_(exec), commit_step_(commit) {}

    ErasedStep execute_step_;
    ErasedStep commit_step_;
};

class PushScheduler {
public:
    template <typename T>
    void add_source(T* component) {
        sources_.push_back(ScheduledComponent::for_type(component));
    }

    template <typename T>
    void add_consumer(T* component) {
        consumers_.push_back(ScheduledComponent::for_type(component));
    }

    void step(SimulationState& st, double dt) {
        assert(dt > 0.0);

        // Execute all sources
        for (auto& e : sources_) {
            e.execute(st, dt);
        }

        // Execute all consumers
        for (auto& e : consumers_) {
            e.execute(st, dt);
        }

        // Commit all scheduled components (deterministic order: sources then consumers)
        for (auto& e : sources_) {
            e.commit(st, dt);
        }

        for (auto& e : consumers_) {
            e.commit(st, dt);
        }
    }

    [[nodiscard]] size_t source_count() const { return sources_.size(); }
    [[nodiscard]] size_t consumer_count() const { return consumers_.size(); }

    void clear_consumers() { consumers_.clear(); }

private:
    std::vector<ScheduledComponent> sources_;
    std::vector<ScheduledComponent> consumers_;
};
