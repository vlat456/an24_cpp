#pragma once

#include <cassert>
#include <cstddef>
#include <vector>

struct SimulationState;

struct ComponentEntry {
    using ExecuteFn = void (*)(void* self, SimulationState& st, float dt);
    using CommitFn = void (*)(void* self, SimulationState& st, float dt);

    void* self = nullptr;
    ExecuteFn execute = nullptr;
    CommitFn commit = nullptr;
};

class PushScheduler {
public:
    template <typename T>
    void add_source(T* component) {
        add_component(sources_, component);
    }

    template <typename T>
    void add_consumer(T* component) {
        add_component(consumers_, component);
    }

    void step(SimulationState& st, float dt) {
        assert(dt > 0.0f);

        // Execute all sources
        for (auto& e : sources_) {
            e.execute(e.self, st, dt);
        }

        // Execute all consumers
        for (auto& e : consumers_) {
            e.execute(e.self, st, dt);
        }

        // Commit all scheduled components (deterministic order: sources then consumers)
        for (auto& e : sources_) {
            if (e.commit != nullptr) {
                e.commit(e.self, st, dt);
            }
        }

        for (auto& e : consumers_) {
            if (e.commit != nullptr) {
                e.commit(e.self, st, dt);
            }
        }
    }

    [[nodiscard]] size_t source_count() const { return sources_.size(); }
    [[nodiscard]] size_t consumer_count() const { return consumers_.size(); }

    void clear_consumers() { consumers_.clear(); }

private:
    template <typename T>
    static ComponentEntry make_entry(T* component) {
        return ComponentEntry{
            component,
            [](void* self, SimulationState& st, float dt) {
                if constexpr (requires(T& c) { c.execute(st, dt); }) {
                    static_cast<T*>(self)->execute(st, dt);
                } else {
                    (void)self;
                    (void)st;
                    (void)dt;
                }
            },
            [](void* self, SimulationState& st, float dt) {
                if constexpr (requires(T& c) { c.commit(st, dt); }) {
                    static_cast<T*>(self)->commit(st, dt);
                } else {
                    (void)self;
                    (void)st;
                    (void)dt;
                }
            }
        };
    }

    template <typename T>
    void add_component(std::vector<ComponentEntry>& bucket, T* component) {
        bucket.push_back(make_entry(component));
    }

    std::vector<ComponentEntry> sources_;
    std::vector<ComponentEntry> consumers_;
};
