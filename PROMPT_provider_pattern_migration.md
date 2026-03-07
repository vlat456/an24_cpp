# Задача: Полный рефакторинг компонентов на Provider Pattern

## Критическая важность

Это **большой рефакторинг всей codebase**. Мы меняем архитектуру компонентов от runtime port indices к compile-time constexpr port indices для WASM performance.

**НЕ оставляй старые классы для обратной совместимости!** Переделай всё сразу.

## Контекст

У нас есть C++ flight simulator с компонентами (Battery, Resistor, Switch и т.д.) которые сейчас используют runtime port indices:

```cpp
// СТАРЫЙ КОД (будет УДАЛЕН):
class Battery {
    uint32_t v_in_idx;   // runtime поле
    uint32_t v_out_idx;  // runtime поле

    void solve_electrical(SimulationState& st, float dt) {
        float v_gnd = st->across[v_in_idx];    // indirect access!
        float v_bus = st->across[v_out_idx];   // indirect access!
        // ...
    }
};
```

**Проблема**: indirect access `st->across[this->v_in_idx]` не оптимизируется в `st->across[0]` для WASM.

## Решение: Provider Pattern

Мы реализовали Provider pattern который позволяет:
- **AOT**: compile-time constexpr port indices → direct access
- **JIT**: runtime port indices из JSON → indirect access
- **Single source of truth**: одна логика для обоих режимов

### Что уже работает

**Созданные файлы** (НЕ ТРОГАЙ, они РАБОТАЮТ):
- `src/jit_solver/components/provider.h` - AotProvider и JitProvider
- `src/jit_solver/components/port_registry.h` - enum PortNames со всеми port names

**Тестовые компоненты** (это ПРИМЕРЫ для тебя):
- `src/jit_solver/components/provider_components.h` - примеры реализации
- `tests/provider_test.cpp` - тесты ✅ ALL PASSED

**Результаты ассемблерного анализа**:
- AOT: ~20 инструкций, fully inlined, direct access `st->across[0]`
- JIT: 50+ инструкций, function call, map lookup
- **AOT в 2-3x быстрее native, 3-5x быстрее WASM**

### Пример работающего кода

```cpp
// AOT component с constexpr port indices
using Battery_AOT = Battery<
    AotProvider<
        Binding<PortNames::v_in, 0>,
        Binding<PortNames::v_out, 1>
    >
>;

// JIT component с runtime port indices
using Battery_JIT = Battery<JitProvider>;
bat.provider.set(PortNames::v_in, 0);  // runtime
bat.provider.set(PortNames::v_out, 1); // runtime

// Единая логика для обоих:
template <typename Provider>
class Battery {
    Provider provider;

    void solve_electrical(SimulationState& st, float dt) {
        // AOT: provider.get() → constexpr 0 (compile-time)
        // JIT: provider.get() → map lookup (runtime)
        float v_gnd = st->across[provider.get(PortNames::v_in)];
        float v_bus = st->across[provider.get(PortNames::v_out)];
        // ... одна логика для AOT и JIT!
    }
};
```

## Что нужно сделать

**ГЛАВНАЯ ЗАДАЧА**: Переделать `src/jit_solver/components/all.h` и `all.cpp` на template с Provider pattern.

### Файлы которые нужно ИЗМЕНИТЬ:

1. **`src/jit_solver/components/all.h`** - все компоненты переделать в template
2. **`src/jit_solver/components/all.cpp`** - всю логику перенести в template методы
3. **`src/jit_solver/components/provider.h`** - исправить warning в fold expression
4. **`src/codegen/codegen.cpp`** - обновить для работы с template компонентами

### Файлы которые НЕ трогать:

- ✅ `src/jit_solver/components/port_registry.h` - enum PortNames (уже работает)
- ✅ `src/jit_solver/components/provider_components.h` - примеры (для reference)
- ✅ `tests/provider_test.cpp` - тесты примеров (для reference)

### Требования к реализации

1. **Template компоненты**: Каждый компонент должен быть `template <typename Provider> class Component`
2. **Единая логика**: Логика solve_electrical/solve_mechanical/etc должна быть ОДНА для AOT и JIT
3. **Использовать enum PortNames**: Вместо чисел использовать `PortNames::v_in`, `PortNames::v_out` и т.д.
4. **Provider lookup**: `provider.get(PortNames::v_in)` вместо прямого доступа к `_idx` полям
5. **Сохранить параметры**: Все параметры компонента (conductance, v_nominal и т.д.) остаются как есть
6. **Удалить PORTS macro**: Не нужна для template
7. **Удалить virtual/override**: Не нужны для template

### Пример миграции (Battery)

**Было** (СТАРЫЙ КОД - УДАЛИТЬ ЭТО):
```cpp
// В all.h:
class Battery final : public Component {
public:
    PORTS(Battery, v_in, v_out)  // генерирует uint32_t v_in_idx, v_out_idx
    float v_nominal;
    float internal_r;
    float inv_internal_r;

    void solve_electrical(SimulationState& state, float dt) override;
    void pre_load() override;
};

// В all.cpp:
void Battery::solve_electrical(SimulationState& state, float /*dt*/) {
    float v_gnd = state.across[v_in_idx];   // indirect
    float v_bus = state.across[v_out_idx];  // indirect
    float g = inv_internal_r;
    float i = (v_nominal + v_gnd - v_bus) * g;
    i = std::clamp(i, -1000.0f, 1000.0f);
    state.through[v_out_idx] += i;
    state.through[v_in_idx] -= i;
}
```

**Стало** (НОВЫЙ КОД - ТАК НАПИШИ):
```cpp
// В all.h (template определение):
template <typename Provider>
class Battery {
public:
    Provider provider;
    std::string name;
    float v_nominal = 28.0f;
    float internal_r = 0.01f;
    float inv_internal_r = 100.0f;

    // Конструктор для совместимости с существующим кодом
    Battery() = default;

    void solve_electrical(SimulationState& st, float dt);
    void pre_load();
};

// В all.cpp (template реализация):
template <typename Provider>
void Battery<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    // Provider lookup - AOT: constexpr, JIT: runtime
    float v_gnd = st->across[provider.get(PortNames::v_in)];
    float v_bus = st->across[provider.get(PortNames::v_out)];
    float g = inv_internal_r;

    // Thevenin -> Norton: I = (V_nominal + V_gnd - V_bus) / R
    float i = (v_nominal + v_gnd - v_bus) * g;
    i = (i > 1000.0f) ? 1000.0f : ((i < -1000.0f) ? -1000.0f : i);

    st->through[provider.get(PortNames::v_in)] -= i;
    st->through[provider.get(PortNames::v_out)] += i;
}

template <typename Provider>
void Battery<Provider>::pre_load() {
    if (internal_r > 0.0f) {
        inv_internal_r = 1.0f / internal_r;
    } else {
        inv_internal_r = 0.0f;
    }
}
```

### Список ВСЕХ компонентов для миграции (29 штук)

Все компоненты из `src/jit_solver/components/all.h`:

1. ✅ Battery (пример в provider_components.h)
2. ✅ Resistor (пример в provider_components.h)
3. ✅ Load (пример в provider_components.h)
4. ✅ Voltmeter (пример в provider_components.h)
5. ✅ IndicatorLight (пример в provider_components.h)
6. ✅ Comparator (пример в provider_components.h)
7. Switch
8. Relay
9. HoldButton
10. RefNode (no-op, просто wire junction)
11. Bus (no-op, просто wire junction)
12. Generator
13. GS24
14. Transformer
15. Inverter
16. LerpNode
17. Splitter (использует alias ports)
18. HighPowerLoad
19. Gyroscope
20. AGK47
21. ElectricPump
22. SolenoidValve
23. InertiaNode
24. TempSensor
25. ElectricHeater
26. RUG82
27. DMR400
28. RU19A
29. Radiator

### Порты и PortNames

enum PortNames уже сгенерирован в `port_registry.h`:
```cpp
enum class PortNames : uint32_t {
    Va, Vb, ac_out, brightness, control, ctrl, dc_in,
    flow_in, flow_out, heat_in, heat_out, i, input,
    k_mod, lamp, o, o1, o2, output, p_out, power,
    primary, rpm_out, secondary, state, t4_out, temp_in,
    temp_out, v, v_bus, v_gen, v_gen_ref, v_in, v_out, v_start
};
```

### Особые случаи

**Splitter** - имеет alias ports (o1, o2 это alias для out):
```cpp
// Убедись что alias ports не используются в provider.get()
// Используй только реальные порты
```

**Switch/Relay** - используют downstream_g из post_step:
```cpp
// Сохранить всю логику post_step
// Заменить _idx на provider.get()
```

**HoldButton** - сохраняет state:
```cpp
// Сохранить все поля: last_control, is_pressed
// Заменить только _idx на provider.get()
```

**RefNode/Bus** - no-op компоненты:
```cpp
// Можно оставить пустыми или вообще убрать
template <typename Provider>
class RefNode {
    // No-op, just wire junction
};
```

## Исправление багов в существующем коде

### 1. Исправить warning в provider.h

В `src/jit_solver/components/provider.h` строка 29:
```cpp
// СТАРЫЙ КОД (выдает warning):
((p == Bindings::key ? (result = Bindings::value, true) : false) || ...);

// НОВЫЙ КОД (без warning):
((p == Bindings::key ? (result = Bindings::value, void(), true) : false) || ...);
// ИЛИ используй fold expression с comma operator:
(..., (p == Bindings::key ? (result = Bindings::value, void()) : void()));
```

### 2. Обновить codegen для работы с template компонентами

В `src/codegen/codegen.cpp` нужно:
1. Для AOT генерировать `Component<AotProvider<...>>`
2. Для JIT генерировать `Component<JitProvider>`
3. Убрать вызовы методов (теперь все inline)

### 3. Обновить все использования компонентов

**jit_solver.h** - убрать Component base class:
```cpp
// СТАРЫЙ КОД:
class Component {
    virtual void solve_electrical(SimulationState& state, float dt) = 0;
};

// НОВЫЙ КОД:
// Component base class больше не нужен!
// Все компоненты - standalone template classes
```

**jit_solver.cpp** - обновить создание компонентов:
```cpp
// СТАРЫЙ КОД:
auto bat = std::make_unique<Battery>(v_in, v_out, 24.0f, 0.01f);

// НОВЫЙ КОД (JIT):
Battery<JitProvider> bat;
bat.provider.set(PortNames::v_in, v_in);
bat.provider.set(PortNames::v_out, v_out);
bat.v_nominal = 24.0f;
bat.internal_r = 0.01f;
```

## Порядок работы

1. **Исправь warning в provider.h** (быстрый win)
2. **Переделай все компоненты в all.h** на template
3. **Перенеси логику из all.cpp** в template методы
4. **Обнови codegen.cpp** для работы с template компонентами
5. **Обнови jit_solver** для работы с template компонентами
6. **Запусти тесты** и убедись что все работает
7. **Проверь ассемблерный код** что AOT дает direct access

## Тестирование

После каждого компонента:
1. Скомпилируй без warnings
2. Запусти `tests/provider_pattern_tests`
3. Проверь что логика не изменилась (те же outputs)
4. Для AOT проверь ассемблерный код (должен быть direct access)

## Финальная проверка

✅ Все 29 компонентов переделаны в template с Provider
✅ Тесты проходят
✅ Нет warnings при компиляции
✅ AOT генерация работает с direct access
✅ JIT работает с runtime flexibility
✅ Ассемблер показывает `st->across[0]` для AOT
✅ Нет дублирования логики (single source of truth)
✅ Удалены все старые классы с _idx полями

## Success Metrics

- **Производительность**: AOT в 2-3x быстрее (ассемблер это покажет)
- **Чистота кода**: нет дублирования, single source of truth
- **Гибкость**: можно использовать AOT или JIT с одним кодом
- **WASM ready**: direct access работает в WASM

## Дополнительные требования

- Используй C++20 features если нужно
- Следуй стилю существующего кода
- Добавь комментарии для сложной логики
- Убедись что код компилируется с `-Wall -Wextra`
-跑了所有 существующие тесты после изменений

## Проблемы которые могут возникнуть

1. **Circular dependency** - template instantiation в header vs cpp
   - Решение: всю template логику в header, или explicit instantiation

2. **Code bloat** - много template instantiated
   - Решение: explicit instantiation для common cases

3. **JIT performance** - map lookup медленный
   - OK: JIT не требует max performance, только flexibility

4. **Compilation time** - templates компилируются медленнее
   - OK: это one-time cost, runtime performance важнее
