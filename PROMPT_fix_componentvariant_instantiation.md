# Задача: Fix ComponentVariant Template Instantiation и Restore Component Fields

## Контекст

**Что было сделано**:
- ✅ Provider pattern реализован (AotProvider + JitProvider)
- ✅ ComponentVariant сгенерирован в `port_registry.h`
- ✅ BuildResult расширен с `std::unordered_map<std::string, ComponentVariant> devices`
- ✅ Factory function `create_component_variant()` создан
- ✅ Editor simulation.cpp обновлен для использования std::visit с multi-domain поддержкой

**Проблемы**:

### 1. Template Instantiation Error
При компиляции `editor_componentvariant_tests` получаем linker errors:
```
ld: symbol(s) not found for architecture arm64
"an24::Battery<JitProvider>::solve_electrical(...)", referenced from:
  std::__1::__variant_detail::__visitation::__base::__dispatcher<...>::__dispatch<...>
```

**Причина**: Template методы для `JitProvider` не инстанциируются. Методы определены в `src/jit_solver/components/all.cpp`, но компилятор не генерирует код для `Battery<JitProvider>::solve_electrical()` и т.д.

### 2. Компоненты были упрощены при рефакторинге
При переходе на Provider pattern многие компоненты потеряли важные поля:

**ElectricPump**:
```cpp
// Было (старый код):
class ElectricPump {
    float speed_pct = 0.0f;        // ПОТЕРЯНО!
    float max_pressure = 1000.0f;  // ✓ Осталось
    void solve_electrical(...);
    void solve_hydraulic(...);
};

// Стало (Provider pattern):
template <typename Provider>
class ElectricPump {
    Provider provider;
    float max_pressure = 1000.0f;
    // ❌ speed_pct потеряно!
};
```

**Generator**:
```cpp
// Было:
class Generator {
    float rpm_input = 0.0f;      // ПОТЕРЯНО!
    float internal_r = 0.005f;   // ✓ Осталось
    float v_nominal = 28.5f;     // ✓ Осталось
};

// Стало:
template <typename Provider>
class Generator {
    Provider provider;
    float internal_r = 0.005f;
    float v_nominal = 28.5f;
    // ❌ rpm_input потеряно!
};
```

**InertiaNode**:
```cpp
// Было:
class InertiaNode {
    float inertia = 1.0f;         // ПОТЕРЯНО!
    float spin_up_inertia = 1.0; // ПОТЕРЯНО!
    float spin_down_inertia = 0.02; // ПОТЕРЯНО!
};

// Стало:
template <typename Provider>
class InertiaNode {
    Provider provider;
    // ❌ Все поля потеряны!
};
```

**И многие другие компоненты...**

Это сломало функциональность! Компоненты теперь не работают правильно.

## Что нужно сделать

### Задача 1: Fix Template Instantiation

**Вариант А: Explicit Template Instantiation (РЕКОМЕНДУЕТСЯ)**

Добавить explicit template instantiation для всех компонентов в конце `all.cpp`:

```cpp
// В конце src/jit_solver/components/all.cpp добавить:

// Explicit template instantiation for JitProvider
// Это заставляет компилятор сгенерировать код для всех template методов

template class Battery<JitProvider>;
template class Switch<JitProvider>;
template class Relay<JitProvider>;
template class Resistor<JitProvider>;
template class Load<JitProvider>;
template class Comparator<JitProvider>;
template class HoldButton<JitProvider>;
template class Generator<JitProvider>;
template class GS24<JitProvider>;
template class Transformer<JitProvider>;
template class Inverter<JitProvider>;
template class LerpNode<JitProvider>;
template class Splitter<JitProvider>;
template class IndicatorLight<JitProvider>;
template class Voltmeter<JitProvider>;
template class HighPowerLoad<JitProvider>;
template class ElectricPump<JitProvider>;
template class SolenoidValve<JitProvider>;
template class InertiaNode<JitProvider>;
template class TempSensor<JitProvider>;
template class ElectricHeater<JitProvider>;
template class Radiator<JitProvider>;
template class DMR400<JitProvider>;
template class RUG82<JitProvider>;
template class RU19A<JitProvider>;
template class Gyroscope<JitProvider>;
template class AGK47<JitProvider>;
template class Bus<JitProvider>;
template class RefNode<JitProvider>;
```

**Почему это работает**: Explicit template instantiation заставляет компилятор сгенерировать код для всех методов класса `Battery<JitProvider>`, включая `solve_electrical()`, `pre_load()`, и т.д.

**Вариант Б: Добавить all.cpp в linker (менее желательно)**

Изменить `src/jit_solver/CMakeLists.txt`:
```cmake
# Instead of:
target_sources(jit_solver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/components/all.cpp
)

# Use object library to force inclusion:
add_library(jit_solver_components OBJECT ${CMAKE_CURRENT_SOURCE_DIR}/components/all.cpp)
target_link_libraries(jit_solver PUBLIC jit_solver_components)
```

**Вариант А предпочтительнее** потому что он более явный и управляемый.

### Задача 2: Restore Lost Component Fields

Для каждого компонента нужно сравнить старую версию (до Provider pattern) с текущей и восстановить потерянные поля.

**Шаги**:
1. `git log --all --full-history --oneline -- "*ElectricPump*"` - найти старую версию
2. Сравнить поля: что было, что есть сейчас
3. Добавить потерянные поля обратно в `src/jit_solver/components/all.h`
4. Обновить `create_component_variant()` в `jit_solver.cpp` чтобы устанавливать эти поля из JSON params

**Пример для ElectricPump**:

```cpp
// В src/jit_solver/components/all.h:
template <typename Provider>
class ElectricPump {
public:
    Provider provider;
    std::string name;

    // Restore lost fields:
    float speed_pct = 0.0f;       // Add back!
    float max_pressure = 1000.0f;

    ElectricPump() = default;
    void solve_electrical(an24::SimulationState& st, float dt);
    void solve_hydraulic(an24::SimulationState& st, float dt);
};
```

```cpp
// В src/jit_solver/jit_solver.cpp, в create_component_variant():
else if (dev.classname == "ElectricPump") {
    ElectricPump<JitProvider> comp;
    setup_component_ports(comp, dev, result);
    comp.speed_pct = get_float(dev, "speed", 0.0f);  // Restore this!
    comp.max_pressure = get_float(dev, "max_pressure", 1000.0f);  // Optional
    return ComponentVariant(std::move(comp));
}
```

**Компоненты для проверки (возможно, потеряны поля)**:
- [ ] ElectricPump - `speed_pct`
- [ ] Generator - `rpm_input`
- [ ] InertiaNode - `inertia`, `spin_up_inertia`, `spin_down_inertia`
- [ ] SolenoidValve - `open_pct` (или аналогичное поле)
- [ ] LerpNode - `in_min`, `in_max`, `out_min`, `out_max`
- [ ] IndicatorLight - `threshold`
- [ ] DMR400 - `hysteresis`
- [ ] RUG82 - `target_voltage`
- [ ] RU19A - `rpm`, `t4`, `fuel_valve_open`, `starting_solenoid_engaged`, `ignition_active`
- [ ] Gyroscope - `angular_velocity`
- [ ] AGK47 - `position_pct`
- [ ] TempSensor - `temp_filtered`
- [ ] ElectricHeater - `power_pct`
- [ ] Radiator - `cooling_factor`
- [ ] HoldButton - `is_pressed` (или `pressed`)
- [ ] Inverter - `duty`
- [ ] HighPowerLoad - `conductance`

### Задача 3: Verify All Components Work

После фиксации:
1. Раскомментировать `editor_componentvariant_tests` в `tests/CMakeLists.txt`
2. Скомпилировать: `cmake --build build --target editor_componentvariant_tests -j8`
3. Запустить: `cd build && ctest -R editor_componentvariant -V`
4. Все тесты должны пройти

## Файлы для изменения

### Задача 1 (Template Instantiation):
1. **src/jit_solver/components/all.cpp** - добавить explicit template instantiation

### Задача 2 (Restore Fields):
1. **src/jit_solver/components/all.h** - добавить потерянные поля
2. **src/jit_solver/jit_solver.cpp** - обновить `create_component_variant()` для установки полей

## Success Criteria

1. ✅ **editor_componentvariant_tests компилируется** без linker errors
2. ✅ **Все тесты проходят** (BuildSimpleBatteryLoadCircuit, MultiDomainComponents, etc.)
3. ✅ **Все компоненты имеют свои оригинальные поля** (как до Provider pattern рефакторинга)
4. ✅ **Factory function устанавливает все поля из JSON params**

## Порядок выполнения

1. **Сначала fix template instantiation** (Задача 1, Вариант А)
   - Это позволит скомпилировать тесты
   - Quick win - 5 минут работы

2. **Потом восстановить поля компонентов** (Задача 2)
   - Сравнить каждый компонент с версией до рефакторинга
   - Добавить потерянные поля
   - Обновить factory function

3. **Тестирование** (Задача 3)
   - Запустить editor_componentvariant_tests
   - Убедиться что все работает

## Важные примечания

1. **Explicit template instantiation** - стандартная практика для template классов
2. **Не удалять код** - если поле было в старой версии, оно должно быть в новой
3. **JSON params** - factory должен читать все params из JSON и устанавливать их в компоненты
4. **Multi-domain** - убедиться что компоненты разных доменов (Electrical, Mechanical, Hydraulic, Thermal) все еще работают

## Дополнительная информация

**Почему template instantiation не работает?**

Template методы компилируются только если используются:
```cpp
// В all.cpp:
template <typename Provider>
void Battery<Provider>::solve_electrical(...) { ... }

// Компилятор видит это, но НЕ генерирует код!
// Код генерируется только когда используется:
Battery<JitProvider> bat;  // ← Теперь компилятор генерирует код
bat.solve_electrical(...);
```

Но если `Battery<JitProvider>::solve_electrical()` используется только через `std::visit` в simulation.cpp, компилятор может не увидеть это использование при компиляции all.cpp, поэтому код не генерируется.

**Explicit template instantiation** решает это:
```cpp
template class Battery<JitProvider>;  // ← Заставляет генерировать код ВСЕХ методов
```

---

**Цель**: Зафиксить ComponentVariant integration, восстановить полную функциональность компонентов и убедиться что все тесты проходят.
