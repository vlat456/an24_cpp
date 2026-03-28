#pragma once

#include <cassert>
#include <cstddef>
#include <vector>

struct SimulationState;

struct ComponentEntry {
    using ExecuteFn = void (*)(void* self, SimulationState& st, float dt);

    void* self = nullptr;
    ExecuteFn execute = nullptr;
};

class PushScheduler {
public:
    template <typename T>
    void add_source(T* component) {
        sources_.push_back(ComponentEntry{
            component,
            [](void* self, SimulationState& st, float dt) {
                if constexpr (requires(T& c) { c.execute(st, dt); }) {
                    static_cast<T*>(self)->execute(st, dt);
                } else {
                    (void)self;
                    (void)st;
                    (void)dt;
                }
            }
        });
    }

    template <typename T>
    void add_consumer(T* component) {
        consumers_.push_back(ComponentEntry{
            component,
            [](void* self, SimulationState& st, float dt) {
                if constexpr (requires(T& c) { c.execute(st, dt); }) {
                    static_cast<T*>(self)->execute(st, dt);
                } else {
                    (void)self;
                    (void)st;
                    (void)dt;
                }
            }
        });
    }

    void step(SimulationState& st, float dt) {
        assert(dt > 0.0f);

        for (auto& e : sources_) {
            e.execute(e.self, st, dt);
        }

        for (auto& e : consumers_) {
            e.execute(e.self, st, dt);
        }
    }

    [[nodiscard]] size_t source_count() const { return sources_.size(); }
    [[nodiscard]] size_t consumer_count() const { return consumers_.size(); }

    void clear_consumers() { consumers_.clear(); }

private:
    std::vector<ComponentEntry> sources_;
    std::vector<ComponentEntry> consumers_;
};
