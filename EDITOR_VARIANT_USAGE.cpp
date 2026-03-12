// ПРИМЕР: Как Editor будет использовать ComponentVariant

#include "jit_solver/components/port_registry.h"
#include "jit_solver/state.h"
#include <unordered_map>
#include <string>


class EditorSimulation {
public:
    // Динамические компоненты - можно добавлять/удалять на лету!
    std::unordered_map<std::string, ComponentVariant> devices;
    SimulationState state;

    // Построить схему из blueprint
    void build(const Blueprint& bp) {
        auto ctx = parse_json(blueprint_to_json(bp));
        auto result = build_systems_dev(ctx.devices, ctx.connections);

        devices.clear();

        // Создать компоненты динамически
        for (const auto& dev : ctx.devices) {
            ComponentVariant variant = create_component(dev, result);
            devices[dev.name] = variant;
        }

        // Allocate signals
        state = SimulationState();
        for (uint32_t i = 0; i < result.signal_count; ++i) {
            state.allocate_signal(0.0f, {Domain::Electrical, false});
        }
    }

    // Шаг симуляции - вызываем solve_electrical для всех компонентов
    void step(float dt) {
        state.clear_through();

        for (auto& [name, variant] : devices) {
            // std::visit вызывает правильный метод без virtual calls!
            std::visit([&](auto& component) {
                component.solve_electrical(state, dt);
            }, variant);
        }

        // SOR solver
        for (size_t i = 0; i < state.across.size(); ++i) {
            if (state.inv_conductance[i] > 0.0f) {
                state.across[i] += state.through[i] * state.inv_conductance[i] * 0.5f;
            }
        }

        // post_step для компонентов которые нуждаются
        for (auto& [name, variant] : devices) {
            std::visit([&](auto& component) {
                if constexpr (requires { component.post_step(state, dt); }) {
                    component.post_step(state, dt);
                }
            }, variant);
        }
    }

    // Добавить компонент интерактивно (drag & drop из palette)
    void add_component(const std::string& type, const std::string& name) {
        // ComponentVariant может хранить любой из 29 типов!
        ComponentVariant variant = create_component_by_type(type, name);
        devices[name] = variant;
    }

    // Удалить компонент
    void remove_component(const std::string& name) {
        devices.erase(name);
    }

private:
    ComponentVariant create_component(
        const DeviceInstance& dev,
        const BuildResult& result
    ) {
        // Factory pattern - создаем компонент по classname
        if (dev.classname == "Battery") {
            Battery<JitProvider> bat;
            bat.provider.set(PortNames::v_in, result.port_to_signal[dev.name + ".v_in"]);
            bat.provider.set(PortNames::v_out, result.port_to_signal[dev.name + ".v_out"]);
            bat.v_nominal = std::stof(dev.params.at("v_nominal"));
            bat.pre_load();
            return bat;
        }
        else if (dev.classname == "Switch") {
            Switch<JitProvider> sw;
            sw.provider.set(PortNames::v_in, result.port_to_signal[dev.name + ".v_in"]);
            sw.provider.set(PortNames::v_out, result.port_to_signal[dev.name + ".v_out"]);
            sw.provider.set(PortNames::control, result.port_to_signal[dev.name + ".control"]);
            sw.provider.set(PortNames::state, result.port_to_signal[dev.name + ".state"]);
            return sw;
        }
        // ... для остальных 27 компонентов
        else {
            throw std::runtime_error("Unknown component type: " + dev.classname);
        }
    }
};

// ПРЕИМУЩЕСТВА:
//
// 1. Интерактивность - добавляй/удаляй компоненты на лету
// 2. Без virtual calls - std::visit оптимизируется компилятором
// 3. Type-safe - компилятор проверяет все типы
// 4. Single source of truth - один код для AOT и JIT
//
// В AOT режиме (production):
// using Component = Battery<AotProvider<PortNames::v_in, 0>, PortNames::v_out, 1>>;
// Компилятор видит: st->across[0] вместо st->across[provider.get(...)]
//
// В JIT режиме (editor):
// using Component = Battery<JitProvider>;
// provider.set(...) - runtime flexible
