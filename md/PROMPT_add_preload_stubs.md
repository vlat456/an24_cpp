# Задача: Добавить `void pre_load() {}` во все компоненты

> **Цель**: Каждый компонент (`cpp_class: true`) должен иметь метод `pre_load()`.
> Это позволяет AOT-кодогенератору вызывать `pre_load()` для ВСЕХ компонентов без хардкод-списка.
> Компоненты, которым нечего предрассчитывать, получают пустой inline-стаб.

---

## Контекст

**Файл:** `src/jit_solver/components/all.h`

Все компоненты — шаблонные классы в файле.
Каждый класс выглядит примерно так:

```cpp
template <typename Provider = JitProvider>
class ИмяКласса {
public:
    static constexpr Domain domain = Domain::Electrical;
    Provider provider;
    // ... поля ...
    ИмяКласса() = default;
    void solve_electrical(SimulationState& st, float dt);
    // возможно post_step, solve_thermal и т.д.
};
```

---

## Что делать

### 1. Найти все классы в `all.h` у которых **НЕТ** `pre_load()`

У 4 классов `pre_load()` уже есть (с реализацией в `all.cpp`):

- `Battery` — `void pre_load();`
- `InertiaNode` — `void pre_load();`
- `AZS` — `void pre_load();`
- `Comparator` — `void pre_load();`

**НЕ ТРОГАЙ эти 4 класса — их `pre_load()` не пустой, он объявлен в `.h` и реализован в `.cpp`.**

### 2. Добавить inline-стаб в каждый оставшийся класс

Добавь **перед закрывающей `};`** каждого класса строку:

```cpp
    void pre_load() {}
```

Это пустой inline-метод. Он ничего не делает, но позволяет вызывать `pre_load()` единообразно.

### 3. Полный список классов, которым нужен стаб (45 штук)

```
Switch, Relay, HoldButton, Resistor, Load, RefNode, Bus,
BlueprintInput, BlueprintOutput, Generator, GS24, Transformer,
Inverter, LerpNode, PID, PD, PI, P, Splitter, Merger,
IndicatorLight, HighPowerLoad, Voltmeter, Gyroscope, AGK47,
ElectricPump, SolenoidValve, InertiaNode — НЕТ (уже есть),
TempSensor, ElectricHeater, RUG82, DMR400, RU19A, Radiator,
Subtract, Multiply, Divide, Add, AND, OR, XOR, NOT, NAND,
Any_V_to_Bool, Positive_V_to_Bool, LUT
```

---

## Пример — до и после

**До (Switch):**

```cpp
template <typename Provider = JitProvider>
class Switch {
public:
    static constexpr Domain domain = Domain::Electrical;
    Provider provider;
    bool closed = false;
    float last_control = 0.0f;
    float downstream_g = 0.0f;
    float downstream_I = 0.0f;
    float v_out_old = 0.0f;
    Switch() = default;
    void solve_electrical(SimulationState& st, float dt);
    void post_step(SimulationState& st, float dt);
};
```

**После (Switch):**

```cpp
template <typename Provider = JitProvider>
class Switch {
public:
    static constexpr Domain domain = Domain::Electrical;
    Provider provider;
    bool closed = false;
    float last_control = 0.0f;
    float downstream_g = 0.0f;
    float downstream_I = 0.0f;
    float v_out_old = 0.0f;
    Switch() = default;
    void solve_electrical(SimulationState& st, float dt);
    void post_step(SimulationState& st, float dt);
    void pre_load() {}
};
```

**Для простых классов (Add):**

```cpp
// До:
template <typename Provider = JitProvider>
class Add {
public:
    static constexpr Domain domain = Domain::Logical;
    Provider provider;
    Add() = default;
    void solve_logical(SimulationState& st, float dt);
};

// После:
template <typename Provider = JitProvider>
class Add {
public:
    static constexpr Domain domain = Domain::Logical;
    Provider provider;
    Add() = default;
    void solve_logical(SimulationState& st, float dt);
    void pre_load() {}
};
```

**Для LUT (последний класс, есть private секция):**

```cpp
// Добавь pre_load() {} в PUBLIC секцию, ПЕРЕД private:
    void solve_logical(SimulationState& st, float dt);
    void pre_load() {}

    static bool parse_table(...);
private:
    static float interpolate(...);
};
```

---

## Что НЕ делать

1. **Не трогай** Battery, InertiaNode, AZS, Comparator — у них `pre_load()` уже объявлен (с реализацией в `all.cpp`).
2. **Не трогай** `all.cpp` — пустые стабы не нужно реализовывать в `.cpp`, они inline.
3. **Не трогай** `explicit_instantiations.h` — существующие инстанциации покрывают все методы.
4. **Не меняй** порядок, не переименовывай, не добавляй логику.
5. **Не добавляй** комментарии типа `// stub` — просто `void pre_load() {}`.

---

## После редактирования `all.h` — обнови codegen

**Файл:** `src/codegen/codegen.cpp`, примерно строка ~420.

**Найди этот блок:**

```cpp
    // Components with pre_load(): Battery, InertiaNode, AZS, Comparator
    static const std::unordered_set<std::string> has_pre_load = {
        "Battery", "InertiaNode", "AZS", "Comparator"
    };
    for (const auto& dev : devices) {
        if (has_pre_load.count(dev.classname)) {
            oss << "    " << sanitize_name(dev.name) << ".pre_load();\n";
        }
    }
```

**Замени на:**

```cpp
    // All cpp_class components have pre_load() (empty stub or real implementation)
    for (const auto& dev : devices) {
        oss << "    " << sanitize_name(dev.name) << ".pre_load();\n";
    }
```

Хардкод-список `has_pre_load` удалён. Теперь codegen вызывает `pre_load()` для КАЖДОГО компонента.

---

## Проверка

```bash
cd /Users/vladimir/an24_cpp/build
make -j$(sysctl -n hw.ncpu)
ctest --output-on-failure
```

Все тесты (997+) должны пройти.

Затем проверь AOT-генерацию:

```bash
./build/examples/codegen_test blueprint.json /tmp/aot_check
grep "pre_load" /tmp/aot_check/aot_test.cpp
```

Каждый компонент блюпринта должен иметь строку `{name}.pre_load()` в `Systems::pre_load()`.

---

## Правило на будущее

При добавлении **любого** нового компонента (см. `md/ADD_NEW_COMPONENT.md`):

- Если компоненту нужен предрасчёт → реализуй `void pre_load();` в `.h`, напиши тело в `.cpp`
- Если не нужен → добавь `void pre_load() {}` inline-стаб в `.h`

**Метод `pre_load()` обязателен для всех компонентов. Без него AOT-кодогенерация не скомпилируется.**
