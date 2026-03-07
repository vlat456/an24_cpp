# Задача: Обновить Editor для работы с ComponentVariant

## Контекст

**Что уже сделано**:
- ✅ Все 29 компонентов переделаны в `template<typename Provider = JitProvider>`
- ✅ Provider pattern реализован (AotProvider + JitProvider)
- ✅ `ComponentVariant` сгенерирован в `port_registry.h`:
  ```cpp
  using ComponentVariant = std::variant<
      Battery<JitProvider>, Switch<JitProvider>, ... // все 29 компонентов
  >;
  ```
- ✅ Visitor helpers: `solve_electrical_visitor`, `post_step_visitor`

**Проблема**: Editor simulation.cpp использует старую архитектуру с `Systems`:
```cpp
// СТАРЫЙ КОД (не работает):
build_result->systems.solve_step(state, step_count, dt);  // ❌ Systems больше нет
```

**Цель**: Editor должен работать с `ComponentVariant` для динамических компонентов.

## Что нужно сделать

### 1. Расширить `BuildResult` (jit_solver.h/cpp)

**Было**:
```cpp
struct BuildResult {
    uint32_t signal_count;
    std::vector<uint32_t> fixed_signals;
    PortToSignal port_to_signal;
    // ❌ Нет устройств!
};
```

**Стало**:
```cpp
struct BuildResult {
    uint32_t signal_count;
    std::vector<uint32_t> fixed_signals;
    PortToSignal port_to_signal;

    // Добавить динамические компоненты:
    std::unordered_map<std::string, ComponentVariant> devices;

    // Для удобства - индекс по типу (опционально):
    std::unordered_map<std::string, std::vector<ComponentVariant*>> devices_by_type;
};
```

**Обновить `build_systems_dev()` в jit_solver.cpp**:
```cpp
BuildResult build_systems_dev(
    const std::vector<DeviceInstance>& devices,
    const std::vector<std::pair<std::string, std::string>>& connections
) {
    // ... existing code for port mapping ...

    BuildResult result;
    result.signal_count = signal_count;
    result.fixed_signals = fixed_signals;
    result.port_to_signal = port_to_signal;

    // ===== НОВАЯ ЧАСТЬ =====
    // Создаем компоненты динамически через factory
    for (const auto& dev : devices) {
        ComponentVariant variant = create_component_variant(dev, result);
        result.devices[dev.name] = variant;

        // Индекс по типу (для быстрого доступа)
        result.devices_by_type[dev.classname].push_back(&result.devices[dev.name]);
    }
    // ===== КОНЕЦ НОВОЙ ЧАСТИ =====

    return result;
}
```

### 2. Создать factory функцию для компонентов

**Добавить в jit_solver.cpp** (или отдельный файл):

```cpp
namespace {

/// Factory function - создает компонент по classname
ComponentVariant create_component_variant(
    const DeviceInstance& dev,
    const BuildResult& result
) {
    // Factory pattern - создаем компонент по classname
    // Каждый компонент устанавливает port indices через provider.set()

    #define CREATE_COMPONENT(Classname) \
        do { \
            Classname<JitProvider> comp; \
            setup_component_ports(comp, dev, result); \
            setup_component_params(comp, dev); \
            comp.pre_load(); \
            return ComponentVariant(std::move(comp)); \
        } while(0)

    if (dev.classname == "Battery") {
        CREATE_COMPONENT(Battery);
    }
    else if (dev.classname == "Switch") {
        CREATE_COMPONENT(Switch);
    }
    else if (dev.classname == "Relay") {
        CREATE_COMPONENT(Relay);
    }
    else if (dev.classname == "Resistor") {
        CREATE_COMPONENT(Resistor);
    }
    else if (dev.classname == "Load") {
        CREATE_COMPONENT(Load);
    }
    else if (dev.classname == "Comparator") {
        CREATE_COMPONENT(Comparator);
    }
    else if (dev.classname == "HoldButton") {
        CREATE_COMPONENT(HoldButton);
    }
    else if (dev.classname == "Generator") {
        CREATE_COMPONENT(Generator);
    }
    else if (dev.classname == "GS24") {
        CREATE_COMPONENT(GS24);
    }
    else if (dev.classname == "Transformer") {
        CREATE_COMPONENT(Transformer);
    }
    else if (dev.classname == "Inverter") {
        CREATE_COMPONENT(Inverter);
    }
    else if (dev.classname == "LerpNode") {
        CREATE_COMPONENT(LerpNode);
    }
    else if (dev.classname == "Splitter") {
        CREATE_COMPONENT(Splitter);
    }
    else if (dev.classname == "IndicatorLight") {
        CREATE_COMPONENT(IndicatorLight);
    }
    else if (dev.classname == "Voltmeter") {
        CREATE_COMPONENT(Voltmeter);
    }
    else if (dev.classname == "HighPowerLoad") {
        CREATE_COMPONENT(HighPowerLoad);
    }
    else if (dev.classname == "ElectricPump") {
        CREATE_COMPONENT(ElectricPump);
    }
    else if (dev.classname == "SolenoidValve") {
        CREATE_COMPONENT(SolenoidValve);
    }
    else if (dev.classname == "InertiaNode") {
        CREATE_COMPONENT(InertiaNode);
    }
    else if (dev.classname == "TempSensor") {
        CREATE_COMPONENT(TempSensor);
    }
    else if (dev_name == "ElectricHeater") {
        CREATE_COMPONENT(ElectricHeater);
    }
    else if (dev.classname == "Radiator") {
        CREATE_COMPONENT(Radiator);
    }
    else if (dev.classname == "DMR400") {
        CREATE_COMPONENT(DMR400);
    }
    else if (dev.classname == "RUG82") {
        CREATE_COMPONENT(RUG82);
    }
    else if (dev.classname == "RU19A") {
        CREATE_COMPONENT(RU19A);
    }
    else if (dev.classname == "Gyroscope") {
        CREATE_COMPONENT(Gyroscope);
    }
    else if (dev.classname == "AGK47") {
        CREATE_COMPONENT(AGK47);
    }
    else if (dev.classname == "Bus" || dev.classname == "RefNode") {
        // Bus/RefNode - no-op компоненты
        // Можно создать пустой или вообще не добавлять
        // Но для consistency добавим
        if (dev.classname == "Bus") {
            Bus<JitProvider> bus;
            setup_component_ports(bus, dev, result);
            return ComponentVariant(std::move(bus));
        } else {
            RefNode<JitProvider> ref;
            setup_component_ports(ref, dev, result);
            return ComponentVariant(std::move(ref));
        }
    }
    else {
        throw std::runtime_error("Unknown component type: " + dev.classname);
    }
}

// Helper function - setup port indices from provider
template <typename T>
void setup_component_ports(T& comp, const DeviceInstance& dev, const BuildResult& result) {
    // Для каждого порта компонента найти его signal index
    // Использовать метаинформацию о портах из component definition

    // Получаем port definition для этого компонента
    auto port_defs = get_component_ports(dev.classname);

    // Устанавливаем port indices
    for (const auto& port_name : port_defs) {
        std::string port_key = dev.name + "." + port_name;
        auto it = result.port_to_signal.find(port_key);
        if (it != result.port_to_signal.end()) {
            // Преобразуем port_name в PortNames enum
            PortNames port_enum = string_to_port_name(port_name);
            comp.provider.set(port_enum, it->second);
        }
    }
}

// Helper function - setup component params from JSON
template <typename T>
void setup_component_params(T& comp, const DeviceInstance& dev) {
    // Установить параметры из dev.params
    // Использовать константные имена параметров

    if (dev.classname == "Battery") {
        if (dev.params.count("v_nominal")) {
            comp.v_nominal = std::stof(dev.params.at("v_nominal"));
        }
        if (dev.params.count("internal_r")) {
            comp.internal_r = std::stof(dev.params.at("internal_r"));
        }
    }
    else if (dev.classname == "Resistor") {
        if (dev.params.count("conductance")) {
            comp.conductance = std::stof(dev.params.at("conductance"));
        }
    }
    else if (dev.classname == "Switch") {
        if (dev.params.count("initial_state")) {
            comp.closed = dev.params.at("initial_state") == "true";
        }
    }
    // ... остальные компоненты
}

// Helper function - convert string to PortNames enum
PortNames string_to_port_name(const std::string& port_name) {
    // Map from string to PortNames enum
    static const std::unordered_map<std::string, PortNames> map = {
        {"v_in", PortNames::v_in},
        {"v_out", PortNames::v_out},
        {"control", PortNames::control},
        {"state", PortNames::state},
        {"input", PortNames::input},
        {"output", PortNames::output},
        {"o", PortNames::o},
        {"o1", PortNames::o1},
        {"o2", PortNames::o2},
        {"i", PortNames::i},
        {"Va", PortNames::Va},
        {"Vb", PortNames::Vb},
        {"primary", PortNames::primary},
        {"secondary", PortNames::secondary},
        {"ac_out", PortNames::ac_out},
        {"dc_in", PortNames::dc_in},
        {"lamp", PortNames::lamp},
        {"power", PortNames::power},
        {"v", PortNames::v},
        {"v_bus", PortNames::v_bus},
        {"v_gen", PortNames::v_gen},
        {"v_gen_ref", PortNames::v_gen_ref},
        {"v_start", PortNames::v_start},
        {"k_mod", PortNames::k_mod},
        {"rpm_out", PortNames::rpm_out},
        {"t4_out", PortNames::t4_out},
        {"temp_in", PortNames::temp_in},
        {"temp_out", PortNames::temp_out},
        {"heat_in", PortNames::heat_in},
        {"heat_out", PortNames::heat_out},
        {"ctrl", PortNames::ctrl},
        {"flow_in", PortNames::flow_in},
        {"flow_out", PortNames::flow_out},
        {"p_out", PortNames::p_out},
        {"brightness", PortNames::brightness}
    };

    auto it = map.find(port_name);
    if (it != map.end()) {
        return it->second;
    }
    throw std::runtime_error("Unknown port name: " + port_name);
}

} // namespace
```

### 3. Обновить `SimulationController` (simulation.cpp)

**Заменить** использование `build_result->systems` на `build_result->devices`:

```cpp
void SimulationController::build(const Blueprint& bp) {
    // ... existing JSON parsing ...

    // СТАРЫЙ КОД:
    // build_result = build_systems_dev(ctx.devices, connections);
    // state.allocate_signals(...);

    // НОВЫЙ КОД:
    build_result = build_systems_dev(ctx.devices, connections);

    // Allocate signals
    state = SimulationState();
    for (uint32_t i = 0; i < build_result->signal_count; ++i) {
        bool is_fixed = std::binary_search(
            build_result->fixed_signals.begin(),
            build_result->fixed_signals.end(), i);
        state.allocate_signal(0.0f, {Domain::Electrical, is_fixed});
    }

    // Initialize fixed signals from RefNodes
    for (auto& [name, variant] : build_result->devices) {
        std::visit([&](auto& comp) {
            if constexpr (std::is_same_v<decltype(comp), RefNode<JitProvider>>) {
                float value = 0.0f;
                // TODO: получить value из JSON params
                auto it = build_result->port_to_signal.find(name + ".v");
                if (it != build_result->port_to_signal.end()) {
                    state.across[it->second] = value;
                }
            }
        }, variant);
    }
}

void SimulationController::step(float dt) {
    if (!build_result.has_value()) return;

    state.clear_through();

    // СТАРЫЙ КОД:
    // build_result->systems.solve_step(state, step_count, dt);

    // НОВЫЙ КОД:
    // Вызываем solve_electrical/solve_logical/etc для всех компонентов
    for (auto& [name, variant] : build_result->devices) {
        std::visit([&](auto& comp) {
            // Универсальный visitor - вызывает нужный solve метод
            if constexpr (requires { comp.solve_electrical(state, dt); }) {
                comp.solve_electrical(state, dt);
            } else if constexpr (requires { comp.solve_logical(state, dt); }) {
                comp.solve_logical(state, dt);
            } else if constexpr (requires { comp.solve_mechanical(state, dt); }) {
                comp.solve_mechanical(state, dt);
            } else if constexpr (requires { comp.solve_hydraulic(state, dt); }) {
                comp.solve_hydraulic(state, dt);
            } else if constexpr (requires { comp.solve_thermal(state, dt); }) {
                comp.solve_thermal(state, dt);
            }
        }, variant);
    }

    state.precompute_inv_conductance();

    // SOR solver (оставляем как есть)
    for (size_t i = 0; i < state.across.size(); ++i) {
        if (!state.signal_types[i].is_fixed && state.inv_conductance[i] > 0.0f) {
            state.across[i] += state.through[i] * state.inv_conductance[i] * omega;
        }
    }

    // post_step для компонентов которые нуждаются
    for (auto& [name, variant] : build_result->devices) {
        std::visit([&](auto& comp) {
            if constexpr (requires { comp.post_step(state, dt); }) {
                comp.post_step(state, dt);
            }
        }, variant);
    }

    time += dt;
    step_count++;
}
```

**Удалить** или **закомментировать** старое использование `systems`:
- `build_result->systems.solve_step()` - удалить
- `build_result->systems.post_step()` - удалить

### 4. Обновить остальные методы SimulationController

**Оставить как есть** - эти методы не зависят от Systems:
- `get_wire_voltage()` - использует `port_to_signal` ✓
- `get_port_value()` - вызывает `get_wire_voltage()` ✓
- `wire_is_energized()` - вызывает `get_wire_voltage()` ✓
- `apply_overrides()` - использует `port_to_signal` ✓
- `reset()` - не зависит от компонентов ✓

### 5. Раскомментировать Editor тесты

**В tests/CMakeLists.txt** раскомментировать:
- `editor_data_tests`
- `editor_persist_tests`
- `editor_render_tests` (если работает)
- Другие editor тесты

**Проверить что компилируется**:
```bash
cmake --build . --target editor_persist_tests -j8
```

### 6. Тестирование

**Создать тест** `tests/editor_componentvariant_test.cpp`:
```cpp
TEST(EditorSimulation, ComponentVariantWorks) {
    SimulationController sim;

    Blueprint bp;
    // ... создать простой blueprint с Battery + Load

    sim.build(bp);

    // Несколько шагов
    for (int i = 0; i < 100; ++i) {
        sim.step(0.001f);
    }

    // Проверить напряжения
    EXPECT_GT(sim.get_wire_voltage("bat1.v_out"), 20.0f);
}
```

## Детали для реализации

### Port mapping в `setup_component_ports()`

**Проблема**: компоненты имеют разные порты, нужно маппинг строка → PortNames.

**Решение**: использовать `get_component_ports()` из port_registry.h:
```cpp
auto port_defs = get_component_ports(dev.classname);
// Возвращает: {"v_in", "v_out"} для Battery
```

Затем для каждого порта:
```cpp
PortNames port_enum = string_to_port_name(port_name);
```

### Handling исключительных портов

**Comparator** имеет порты `Va`, `Vb` (не стандартные):
```cpp
if (dev.classname == "Comparator") {
    Comparator<JitProvider> comp;

    // Va, Vb, o порты
    comp.provider.set(PortNames::Va, result.port_to_signal[dev.name + ".Va"]);
    comp.provider.set(PortNames::Vb, result.port_to_signal[dev.name + ".Vb"]);
    comp.provider.set(PortNames::o, result.port_to_signal[dev.name + ".o"]);

    // Параметры
    comp.Von = 0.1f;
    comp.Voff = -0.1f;

    return ComponentVariant(std::move(comp));
}
```

### Type-safe visitor для разных доменов

```cpp
// Электрика (60 Hz)
if constexpr (requires { comp.solve_electrical(state, dt); }) {
    comp.solve_electrical(state, dt);
}
// Механика (20 Hz)
else if constexpr (requires { comp.solve_mechanical(state, dt); }) {
    comp.solve_mechanical(state, dt);
}
// Логика (60 Hz)
else if constexpr (requires { comp.solve_logical(state, dt); }) {
    comp.solve_logical(state, dt);
}
// И т.д.
```

## Success Criteria

1. ✅ **BuildResult расширен** - содержит `devices` map
2. ✅ **Factory function** - создает все 29 компонентов
3. ✅ **SimulationController::build()** - использует ComponentVariant
4. ✅ **SimulationController::step()** - вызывает solve через std::visit
5. ✅ **Editor тесты** компилируются и запускаются
6. ✅ **Manual тест** - создать blueprint, построить, сделать шаг, проверить напряжения

## Файлы для изменения

1. **src/jit_solver/jit_solver.h** - добавить `devices` в BuildResult
2. **src/jit_solver/jit_solver.cpp** - реализовать `build_systems_dev()` с factory
3. **src/editor/simulation.cpp** - обновить `build()`, `step()`, убрать `systems`
4. **tests/CMakeLists.txt** - раскомментировать editor тесты
5. **Создать тест** - `tests/editor_componentvariant_test.cpp`

## Ограничения

- **НЕ трогать** AOT codegen (не нужно сейчас)
- **НЕ трогать** Provider pattern (уже работает)
- **НЕ добавлять** новые типы в PortNames (уже есть все)
- **НЕ менять** интерфейс SimulationState (остается как есть)

## Важные примечания

1. **std::visit == direct call**: Компилятор оптимизирует std::visit в прямой вызов без overhead
2. **constexpr checks**: `if constexpr` проверяется compile-time, zero runtime cost
3. **Type-safe**: Compiler проверяет все 29 типов
4. **Flexible**: Можно добавлять/удалять компоненты интерактивно

## Дополнительные улучшения (опционально)

Если останется время:

1. **Оптимизация port mapping** - кэшировать `string_to_port_name()`
2. **Error handling** - добавить проверку на неизвестные компоненты
3. **Debug output** - логировать создаваемые компоненты
4. **Performance** - замерить скорость с ComponentVariant vs старый Systems

## Пример использования

```cpp
// В Editor:
SimulationController sim;
sim.build(blueprint);  // Создаст компоненты из JSON

// Интерактивное добавление:
auto& dev = add_component("Battery", "bat2");
set_param(dev, "v_nominal", "24.0f");

sim.step(0.001f);  // Вызовет solve для всех компонентов

float v = sim.get_wire_voltage("bat2.v_out");  // Читает из state
```

---

**Цель**: Editor должен работать с ComponentVariant так же интерактивно как раньше работал с Systems, но без virtual calls и с возможностью динамически добавлять/удалять компоненты.
