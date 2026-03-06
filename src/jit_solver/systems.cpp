#include "systems.h"
#include "state.h"
#include "../json_parser/json_parser.h"
#include <spdlog/spdlog.h>

namespace an24 {

void Systems::add_component(std::unique_ptr<Component> comp, const std::vector<Domain>& domains) {
    Component* raw_ptr = comp.get();  // Save pointer before moving
    all_components.push_back(std::move(comp));  // Centralized ownership

    // Register component in each specified domain
    for (Domain domain : domains) {
        switch (domain) {
            case Domain::Electrical:
                electrical.push_back(raw_ptr);
                break;
            case Domain::Hydraulic:
                // Add to first bucket (could be smarter later)
                hydraulic[0].push_back(raw_ptr);
                break;
            case Domain::Mechanical:
                mechanical[0].push_back(raw_ptr);
                break;
            case Domain::Thermal:
                thermal[0].push_back(raw_ptr);
                break;
        }
    }

    spdlog::debug("[Systems] Added component to {} domains", domains.size());
}

void Systems::add_electrical(std::unique_ptr<Component> comp) {
    electrical.push_back(comp.get());
    all_components.push_back(std::move(comp));
}

void Systems::add_hydraulic(size_t bucket, std::unique_ptr<Component> comp) {
    Component* raw_ptr = comp.get();
    hydraulic[bucket % 12].push_back(raw_ptr);
    all_components.push_back(std::move(comp));
}

void Systems::add_mechanical(size_t bucket, std::unique_ptr<Component> comp) {
    Component* raw_ptr = comp.get();
    mechanical[bucket % 3].push_back(raw_ptr);
    all_components.push_back(std::move(comp));
}

void Systems::add_thermal(size_t bucket, std::unique_ptr<Component> comp) {
    Component* raw_ptr = comp.get();
    thermal[bucket % 60].push_back(raw_ptr);
    all_components.push_back(std::move(comp));
}

size_t Systems::component_count() const {
    return all_components.size();
}

void Systems::solve_step(SimulationState& state, size_t step, float dt) {
    // Accumulate dt for each domain (FPS-independent physics)
    accumulator_mechanical += dt;
    accumulator_hydraulic += dt;
    accumulator_thermal += dt;

    // Electrical: every step (no accumulation needed)
    for (auto& comp : electrical) {
        comp->solve_electrical(state, dt);
    }

    // Mechanical: every 3rd step - use accumulated time, then reset
    if (step % 3 == 0) {
        size_t bucket = (step / 3) % 3;
        for (auto& comp : mechanical[bucket]) {
            comp->solve_mechanical(state, accumulator_mechanical);
        }
        accumulator_mechanical = 0.0f;  // Reset after use
    }

    // Hydraulic: every 12th step - use accumulated time, then reset
    if (step % 12 == 0) {
        size_t bucket = (step / 12) % 12;
        for (auto& comp : hydraulic[bucket]) {
            comp->solve_hydraulic(state, accumulator_hydraulic);
        }
        accumulator_hydraulic = 0.0f;  // Reset after use
    }

    // Thermal: every 60th step - use accumulated time, then reset
    if (step % 60 == 0) {
        for (auto& bucket : thermal) {
            for (auto& comp : bucket) {
                comp->solve_thermal(state, accumulator_thermal);
            }
        }
        accumulator_thermal = 0.0f;  // Reset after use
    }
}

void Systems::post_step(SimulationState& state, float dt) {
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
