#include "systems.h"
#include "state.h"
#include <spdlog/spdlog.h>

namespace an24 {

void Systems::add_electrical(std::unique_ptr<Component> comp) {
    electrical.push_back(std::move(comp));
}

void Systems::add_hydraulic(size_t bucket, std::unique_ptr<Component> comp) {
    hydraulic[bucket % 12].push_back(std::move(comp));
}

void Systems::add_mechanical(size_t bucket, std::unique_ptr<Component> comp) {
    mechanical[bucket % 3].push_back(std::move(comp));
}

void Systems::add_thermal(size_t bucket, std::unique_ptr<Component> comp) {
    thermal[bucket % 60].push_back(std::move(comp));
}

size_t Systems::component_count() const {
    size_t count = electrical.size();
    for (const auto& bucket : hydraulic) count += bucket.size();
    for (const auto& bucket : mechanical) count += bucket.size();
    for (const auto& bucket : thermal) count += bucket.size();
    return count;
}

void Systems::solve_step(SimulationState& state, size_t step) {
    // Electrical: every step (60 Hz)
    for (auto& comp : electrical) {
        comp->solve_electrical(state);
    }

    // Mechanical: every 3rd step (20 Hz)
    if (step % 3 == 0) {
        size_t bucket = (step / 3) % 3;
        for (auto& comp : mechanical[bucket]) {
            comp->solve_mechanical(state);
        }
    }

    // Hydraulic: every 12th step (5 Hz)
    if (step % 12 == 0) {
        size_t bucket = (step / 12) % 12;
        for (auto& comp : hydraulic[bucket]) {
            comp->solve_hydraulic(state);
        }
    }

    // Thermal: every 60th step (1 Hz)
    if (step % 60 == 0) {
        size_t bucket = (step / 60) % 60;
        for (auto& comp : thermal[bucket]) {
            comp->solve_thermal(state);
        }
    }
}

void Systems::post_step(const SimulationState& state, float dt) {
    for (auto& comp : electrical) {
        comp->post_step(state, dt);
    }
    for (auto& bucket : hydraulic) {
        for (auto& comp : bucket) {
            comp->post_step(state, dt);
        }
    }
    for (auto& bucket : mechanical) {
        for (auto& comp : bucket) {
            comp->post_step(state, dt);
        }
    }
    for (auto& bucket : thermal) {
        for (auto& comp : bucket) {
            comp->post_step(state, dt);
        }
    }
}

void Systems::pre_load() {
    for (auto& comp : electrical) {
        comp->pre_load();
    }
    for (auto& bucket : hydraulic) {
        for (auto& comp : bucket) {
            comp->pre_load();
        }
    }
    for (auto& bucket : mechanical) {
        for (auto& comp : bucket) {
            comp->pre_load();
        }
    }
    for (auto& bucket : thermal) {
        for (auto& comp : bucket) {
            comp->pre_load();
        }
    }
}

} // namespace an24
