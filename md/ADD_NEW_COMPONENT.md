# Инструкция: Добавление нового компонента

Пошаговая инструкция для AI-агента по добавлению нового компонента в симулятор АН-24.
Компонент должен работать в JIT (runtime) режиме через шаблонный Provider-паттерн.

> **ВАЖНО**: Эта инструкция написана для AI-агента с ограниченным контекстом.
> Каждый шаг содержит точные имена файлов, точный формат вставки, и примеры из существующего кода.
> Следуй шагам строго по порядку, не пропуская ни одного.

---

## Обзор: архитектура компонента

Компонент в этом проекте — это шаблонный C++ класс, который:

1. Определяется в JSON файле (`library/<category>/<Name>.json`)
2. Объявляется в `src/jit_solver/components/all.h`
3. Реализуется в `src/jit_solver/components/all.cpp`
4. Регистрируется в фабрике `src/jit_solver/jit_solver.cpp`
5. Добавляется в explicit instantiation файл

**Порты** компонента — это точки подключения к другим компонентам. Каждый порт получает
индекс в массиве `SimulationState.across[]` (потенциалы) / `SimulationState.through[]` (токи).

**Provider** — паттерн для маппинга портов:

- `JitProvider` — runtime HashMap lookup (используется в редакторе)
- `AotProvider` — compile-time constexpr (используется в AOT-генерации)

Шаблонный параметр позволяет одному коду работать в обоих режимах.

---

## Сводная таблица файлов

| #   | Файл                                                  | Действие                                          |
| --- | ----------------------------------------------------- | ------------------------------------------------- |
| 1   | `library/<category>/<Name>.json`                      | **Создать** — JSON-определение                    |
| 2   | `src/jit_solver/components/all.h`                     | **Добавить** — объявление класса                  |
| 3   | `src/jit_solver/components/all.cpp`                   | **Добавить** — реализацию методов                 |
| 4   | `src/jit_solver/components/explicit_instantiations.h` | **Добавить** — строку инстанциации                |
| 5   | `src/jit_solver/jit_solver.cpp`                       | **Добавить** — case в фабрику                     |
| 6   | `src/jit_solver/components/port_registry.h`           | **Запустить codegen** (НЕ редактировать вручную!) |
| 7   | `tests/`                                              | **Создать** — юнит-тесты (опционально)            |

---

## Шаг 0: Выбрать категорию

Компоненты хранятся в `library/` в подпапках по категориям:

```
library/
├── electrical/    — Battery, Resistor, Switch, Relay, Generator, Voltmeter, ...
├── logical/       — AND, OR, NOT, PID, Comparator, LUT, ...
├── math/          — Add, Subtract, Multiply, Divide, LerpNode
├── mechanical/    — InertiaNode
├── thermal/       — TempSensor
├── systems/       — RU19A, DMR400, GS24, RUG82, AGK47
├── Bus.json, RefNode.json, Splitter.json, Merger.json  — инфраструктурные
├── Group.json, Text.json  — visual_only (без симуляции)
└── BlueprintInput.json, BlueprintOutput.json  — порты вложенных блюпринтов
```

Выбери подпапку по основному домену компонента.

---

## Шаг 1: Создать JSON-определение

Создай файл `library/<category>/<Name>.json`.

### Точный формат

```json
{
  "classname": "ИмяКласса",
  "description": "Описание компонента на английском",
  "cpp_class": true,
  "ports": {
    "имя_порта": {
      "direction": "In|Out|InOut",
      "type": "V|I|Bool|RPM|Temperature|Pressure|Position|Any"
    }
  },
  "params": {
    "имя_параметра": "значение_по_умолчанию_строкой"
  },
  "domains": ["Electrical"],
  "priority": "med",
  "critical": false
}
```

### Обязательные поля

| Поле        | Тип      | Описание                                                                                |
| ----------- | -------- | --------------------------------------------------------------------------------------- |
| `classname` | string   | Имя C++ класса. Должно совпадать ТОЧНО с именем в `all.h`                               |
| `cpp_class` | bool     | `true` = у компонента есть C++ класс. `false` = это blueprint (композитный)             |
| `ports`     | object   | Порты. Ключ = имя порта (используется в `PortNames::xxx`)                               |
| `params`    | object   | Параметры. Ключ = имя, значение = строка с дефолтом                                     |
| `domains`   | string[] | Массив доменов: `"Electrical"`, `"Logical"`, `"Mechanical"`, `"Hydraulic"`, `"Thermal"` |

### Необязательные поля

| Поле           | Тип    | По умолчанию | Описание                                                     |
| -------------- | ------ | ------------ | ------------------------------------------------------------ |
| `priority`     | string | `"med"`      | `"high"`, `"med"`, `"low"` — приоритет вычислений            |
| `critical`     | bool   | `false`      | Критический компонент (всегда вычисляется)                   |
| `content_type` | string | `"None"`     | UI виджет: `"None"`, `"Gauge"`, `"Switch"`, `"Text"`         |
| `size`         | object | авто         | Размер в редакторе: `{ "x": 2.0, "y": 2.0 }` (единицы сетки) |
| `visual_only`  | bool   | `false`      | `true` = нет симуляции (только визуальный, как Group/Text)   |
| `render_hint`  | string | `""`         | Подсказка рендеру: `"bus"`, `"ref"`, `"group"`, `"text"`     |

### Допустимые значения direction

- `"In"` — входной порт (читает voltage/сигнал из `st.across[]`)
- `"Out"` — выходной порт (записывает voltage/сигнал в `st.across[]`)
- `"InOut"` — двунаправленный порт

### Допустимые значения type

| Тип           | Описание                  | Типичный домен |
| ------------- | ------------------------- | -------------- |
| `V`           | Вольтаж (напряжение)      | Electrical     |
| `I`           | Ток                       | Electrical     |
| `Bool`        | Логическое значение (0/1) | Logical        |
| `RPM`         | Обороты в минуту          | Mechanical     |
| `Temperature` | Температура               | Thermal        |
| `Pressure`    | Давление                  | Hydraulic      |
| `Position`    | Позиция                   | Mechanical     |
| `Any`         | Универсальный тип         | Logical, Math  |

### Домены и частоты обновления

| Домен        | Частота                | Метод solve          |
| ------------ | ---------------------- | -------------------- |
| `Electrical` | 60 Hz (каждый шаг)     | `solve_electrical()` |
| `Logical`    | 60 Hz (каждый шаг)     | `solve_logical()`    |
| `Mechanical` | 20 Hz (каждый 3-й шаг) | `solve_mechanical()` |
| `Hydraulic`  | 5 Hz (каждый 12-й шаг) | `solve_hydraulic()`  |
| `Thermal`    | 1 Hz (каждый 60-й шаг) | `solve_thermal()`    |

Компонент может принадлежать нескольким доменам: `"domains": ["Electrical", "Thermal"]`.
В этом случае он реализует несколько `solve_xxx()` методов.

### Примеры реальных JSON (из проекта)

**Простой электрический (Resistor)**:

```json
{
  "classname": "Resistor",
  "description": "Pure conductance element",
  "cpp_class": true,
  "ports": {
    "v_in": { "direction": "In", "type": "V" },
    "v_out": { "direction": "Out", "type": "V" }
  },
  "params": { "conductance": "0.1" },
  "domains": ["Electrical"],
  "priority": "med",
  "critical": false
}
```

**Логический/math (Add)**:

```json
{
  "classname": "Add",
  "description": "Adder: o = A + B",
  "cpp_class": true,
  "ports": {
    "A": { "direction": "In", "type": "Any" },
    "B": { "direction": "In", "type": "Any" },
    "o": { "direction": "Out", "type": "Any" }
  },
  "params": {},
  "domains": ["Logical"],
  "priority": "med",
  "critical": false,
  "size": { "x": 2, "y": 2 }
}
```

**С параметрами и состоянием (Battery)**:

```json
{
  "classname": "Battery",
  "description": "Battery voltage source with internal resistance",
  "cpp_class": true,
  "ports": {
    "v_in": { "direction": "In", "type": "V" },
    "v_out": { "direction": "Out", "type": "V" }
  },
  "params": {
    "v_nominal": "28.0",
    "internal_r": "0.01",
    "inv_internal_r": "100.0",
    "capacity": "1000.0",
    "charge": "1000.0"
  },
  "domains": ["Electrical"],
  "priority": "high",
  "critical": true
}
```

**Мульти-доменный (RU19A)**:

```json
{
  "classname": "RU19A",
  "description": "RU19A-300 APU",
  "cpp_class": true,
  "ports": {
    "v_start":  { "direction": "In",  "type": "V" },
    "v_bus":    { "direction": "Out", "type": "V" },
    "k_mod":    { "direction": "In",  "type": "V" },
    "rpm_out":  { "direction": "Out", "type": "RPM" },
    "t4_out":   { "direction": "Out", "type": "Temperature" }
  },
  "params": { "target_rpm": "16000.0", ... },
  "domains": ["Electrical", "Mechanical", "Thermal"],
  "priority": "high",
  "critical": true
}
```

**С UI-виджетом (Voltmeter)**:

```json
{
  "classname": "Voltmeter",
  "description": "Analog voltmeter gauge",
  "cpp_class": true,
  "ports": { "v_in": { "direction": "In", "type": "V" } },
  "params": {},
  "domains": ["Electrical"],
  "priority": "low",
  "critical": false,
  "content_type": "Gauge"
}
```

**Мульти-доменный с тепловой моделью (AZS)**:

```json
{
  "classname": "AZS",
  "description": "Aircraft circuit breaker with thermal trip",
  "cpp_class": true,
  "ports": {
    "v_in": { "direction": "In", "type": "V" },
    "v_out": { "direction": "Out", "type": "V" },
    "control": { "direction": "In", "type": "Bool" },
    "state": { "direction": "Out", "type": "Bool" },
    "temp": { "direction": "Out", "type": "Temperature" },
    "tripped": { "direction": "Out", "type": "Bool" }
  },
  "params": {
    "closed": "false",
    "i_nominal": "20.0"
  },
  "domains": ["Electrical", "Thermal"],
  "content_type": "Switch"
}
```

---

## Шаг 2: Объявить C++ класс (`src/jit_solver/components/all.h`)

Добавь шаблонный класс внутри `namespace an24 { ... }`.

### Где именно вставить

Файл `all.h` разделён на секции комментариями:

```cpp
// =============================================================================
// Electrical Components
// =============================================================================
// ...
// =============================================================================
// Hydraulic Components
// =============================================================================
// ...
// =============================================================================
// Mechanical Components
// =============================================================================
// ...
// =============================================================================
// Thermal Components
// =============================================================================
```

Вставь новый класс в правильную секцию (по домену).
Логические и Math компоненты находятся после Thermal (перед закрывающей `}` namespace).

### Шаблон класса

```cpp
/// Краткое описание — одна строка
template <typename Provider = JitProvider>
class ИмяКласса {
public:
    static constexpr Domain domain = Domain::Electrical; // ВЫБЕРИ ДОМЕН

    Provider provider;

    // Параметры (из params в JSON, float по умолчанию)
    float param1 = 1.0f;
    float param2 = 0.0f;

    // Внутреннее состояние (НЕ из JSON, инициализируется в runtime)
    float state_var = 0.0f;

    ИмяКласса() = default;

    // ОБЪЯВИ ТОЛЬКО НУЖНЫЕ solve/post_step методы:
    void solve_electrical(an24::SimulationState& st, float dt);   // для Electrical
    void solve_logical(an24::SimulationState& st, float dt);      // для Logical
    void solve_mechanical(an24::SimulationState& st, float dt);   // для Mechanical
    void solve_hydraulic(an24::SimulationState& st, float dt);    // для Hydraulic
    void solve_thermal(an24::SimulationState& st, float dt);      // для Thermal
    void post_step(an24::SimulationState& st, float dt);          // после SOR-итерации

    // pre_load() — ОБЯЗАТЕЛЕН для каждого компонента:
    void pre_load() {}                                             // пустой стаб (если нет предрасчёта)
    // ИЛИ:
    void pre_load();                                               // с реализацией в all.cpp (если есть предрасчёт)
};
```

### Критические правила

1. **`Provider provider;`** — ОБЯЗАТЕЛЬНОЕ поле. Без него порты не работают.
2. **`static constexpr Domain domain = ...;`** — ОБЯЗАТЕЛЬНО. Определяет частоту обновления.
3. **`ИмяКласса() = default;`** — ОБЯЗАТЕЛЬНО. Конструктор по умолчанию.
4. Для мульти-доменных: `Domain::Electrical | Domain::Thermal` (побитовое ИЛИ).
5. Имена параметров должны совпадать с ключами в `params` JSON.
6. **`void pre_load() {}`** — ОБЯЗАТЕЛЬНО для каждого компонента. Если предрасчёт не нужен — пустой inline-стаб. Если нужен — объяви `void pre_load();` в `.h`, реализуй в `.cpp`. Без `pre_load()` AOT-кодогенерация не скомпилируется.
7. Объявляй ТОЛЬКО те `solve_xxx` и `post_step`, которые реализуешь.

### Правила выбора методов

| Что делает компонент                                  | Какой метод реализовать                                   |
| ----------------------------------------------------- | --------------------------------------------------------- |
| Штампует проводимость/ток в матрицу                   | `solve_electrical()` или `solve_<domain>()`               |
| Читает результат SOR и обновляет состояние            | `post_step()`                                             |
| Предрасчёт при инициализации (например `inv_r = 1/r`) | `pre_load()`                                              |
| Пассивный элемент без состояния                       | `solve_<domain>()` + `void pre_load() {}`                 |
| Контроллер (PID, логика)                              | `solve_<domain>()` + `post_step()` + `void pre_load() {}` |

### Реальные примеры из проекта

**Простейший — без параметров (Add)**:

```cpp
template <typename Provider = JitProvider>
class Add {
public:
    static constexpr Domain domain = Domain::Logical;
    Provider provider;
    Add() = default;
    void solve_logical(an24::SimulationState& st, float dt);
    void pre_load() {}
};
```

**С параметрами, без состояния (Resistor)**:

```cpp
template <typename Provider = JitProvider>
class Resistor {
public:
    static constexpr Domain domain = Domain::Electrical;
    Provider provider;
    float conductance = 0.1f;
    Resistor() = default;
    void solve_electrical(an24::SimulationState& st, float dt);
    void pre_load() {}
};
```

**С параметрами и pre_load (Battery)**:

```cpp
template <typename Provider = JitProvider>
class Battery {
public:
    static constexpr Domain domain = Domain::Electrical;
    Provider provider;
    std::string name;
    float capacity = 1000.0f;
    float charge = 1000.0f;
    float internal_r = 0.01f;
    float inv_internal_r = 100.0f;
    float v_nominal = 28.0f;
    Battery() = default;
    void solve_electrical(an24::SimulationState& st, float dt);
    void pre_load();
};
```

**С состоянием (PID)**:

```cpp
template <typename Provider = JitProvider>
class PID {
public:
    static constexpr Domain domain = Domain::Electrical;
    Provider provider;
    // Параметры (from JSON)
    float Kp = 1.0f;
    float Ki = 0.0f;
    float Kd = 0.0f;
    float output_min = -1000.0f;
    float output_max = 1000.0f;
    float filter_alpha = 0.2f;
    // Состояние (runtime, не из JSON)
    float integral = 0.0f;
    float last_error = 0.0f;
    float d_filtered = 0.0f;
    PID() = default;
    void solve_electrical(an24::SimulationState& st, float dt);
    void post_step(an24::SimulationState& st, float dt);
    void pre_load() {}
};
```

**Мульти-доменный (ElectricHeater)**:

```cpp
template <typename Provider = JitProvider>
class ElectricHeater {
public:
    static constexpr Domain domain = Domain::Electrical | Domain::Thermal;
    Provider provider;
    float max_power = 1000.0f;
    float efficiency = 0.9f;
    ElectricHeater() = default;
    void solve_electrical(an24::SimulationState& st, float dt);
    void solve_thermal(an24::SimulationState& st, float dt);
    void pre_load() {}
};
```

**Мульти-доменный с реальным pre_load (AZS — circuit breaker)**:

```cpp
template <typename Provider = JitProvider>
class AZS {
public:
    static constexpr Domain domain = Domain::Electrical | Domain::Thermal;
    Provider provider;
    bool closed = false;
    float last_control = 0.0f;
    float downstream_g = 0.0f;
    float downstream_I = 0.0f;
    float v_out_old = 0.0f;
    float temp = 0.0f;
    float i_nominal = 20.0f;
    float r_heat = 0.0025f;   // 1/(i_nominal²) — precomputed in pre_load()
    float k_cool = 1.0f;
    AZS() = default;
    void solve_electrical(an24::SimulationState& st, float dt);
    void solve_thermal(an24::SimulationState& st, float dt);
    void post_step(an24::SimulationState& st, float dt);
    void pre_load();  // объявление — реализация в all.cpp
};
```

---

## Шаг 3: Реализовать методы (`src/jit_solver/components/all.cpp`)

Добавь реализацию в `all.cpp` ДО строки `#include "explicit_instantiations.h"` (это последняя строка файла).

### Шаблон вставки

```cpp
// =============================================================================
// ИмяКласса
// =============================================================================

template <typename Provider>
void ИмяКласса<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    // ...
}

template <typename Provider>
void ИмяКласса<Provider>::post_step(an24::SimulationState& st, float dt) {
    // ...
}
```

### Как работать с SimulationState

**SimulationState** содержит три основных массива (Structure of Arrays):

```cpp
st.across[idx]       // Потенциал узла (V, T, RPM, ...) — ЧИТАТЬ в post_step, ШТАМПОВАТЬ в solve
st.through[idx]      // Ток/поток через узел — ШТАМПОВАТЬ в solve
st.conductance[idx]  // Проводимость узла — ШТАМПОВАТЬ в solve
```

**Получить индекс порта**:

```cpp
uint32_t idx = provider.get(PortNames::имя_порта);
```

`PortNames` — это auto-generated enum в `port_registry.h`. Имена совпадают с ключами в `ports` JSON.

### Stamp-хелперы (defined in `state.h`)

Все хелперы — `inline`, можно вызывать прямо в solve-методах:

```cpp
// 1. Двухпортовый проводник (резистор между двумя узлами)
// Ток из idx1 в idx2 = (V2 - V1) * g
stamp_two_port(st.conductance.data(), st.through.data(), st.across.data(),
               idx1, idx2, g);

// 2. Однопортовая нагрузка на землю
// Ток из idx в GND = V * g (стекает напряжение)
stamp_one_port_ground(st.conductance.data(), st.through.data(), st.across.data(),
                      idx, g);

// 3. Источник тока (Norton)
// Добавляет проводимость g и ток i_source на узел idx
stamp_current_source(st.conductance.data(), st.through.data(),
                     idx, g, i_source);

// 4. Источник напряжения (Thevenin → Norton)
// V_source через R_internal
stamp_voltage_source(st.conductance.data(), st.through.data(), st.across.data(),
                     idx, v_source, r_internal);
```

### Паттерны реализации (копируй подходящий)

#### Паттерн 1: Двухпортовый пассивный элемент (Resistor)

```cpp
template <typename Provider>
void Resistor<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    stamp_two_port(st.conductance.data(), st.through.data(), st.across.data(),
                   provider.get(PortNames::v_out), provider.get(PortNames::v_in), conductance);
}
```

#### Паттерн 2: Однопортовая нагрузка (Load)

```cpp
template <typename Provider>
void Load<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    stamp_one_port_ground(st.conductance.data(), st.through.data(), st.across.data(),
                          provider.get(PortNames::input), conductance);
}
```

#### Паттерн 3: Источник напряжения (Battery)

```cpp
template <typename Provider>
void Battery<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    stamp_two_port(st.conductance.data(), st.through.data(), st.across.data(),
                   provider.get(PortNames::v_out), provider.get(PortNames::v_in), inv_internal_r);
    st.through[provider.get(PortNames::v_out)] += v_nominal * inv_internal_r;
    st.through[provider.get(PortNames::v_in)]  -= v_nominal * inv_internal_r;
}
```

#### Паттерн 4: Логический/Math (Add — прямая запись в across)

```cpp
template <typename Provider>
void Add<Provider>::solve_logical(an24::SimulationState& st, float /*dt*/) {
    float A = st.across[provider.get(PortNames::A)];
    float B = st.across[provider.get(PortNames::B)];
    st.across[provider.get(PortNames::o)] = A + B;
}
```

#### Паттерн 5: Логический гейт (AND)

```cpp
template <typename Provider>
void AND<Provider>::solve_logical(an24::SimulationState& st, float /*dt*/) {
    float A = st.across[provider.get(PortNames::A)];
    float B = st.across[provider.get(PortNames::B)];
    bool a = (A > 0.5f);   // Порог: > 0.5V = TRUE
    bool b = (B > 0.5f);
    st.across[provider.get(PortNames::o)] = (a && b) ? 1.0f : 0.0f;
}
```

#### Паттерн 6: Контроллер с post_step (PID — solve ставит минимальную проводимость, post_step пишет результат)

```cpp
template <typename Provider>
void PID<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    // Минимальная проводимость чтобы SOR "видел" выходной узел
    st.conductance[provider.get(PortNames::output)] += 1e-6f;
}

template <typename Provider>
void PID<Provider>::post_step(an24::SimulationState& st, float dt) {
    float setpoint = st.across[provider.get(PortNames::setpoint)];
    float feedback = st.across[provider.get(PortNames::feedback)];
    float error = setpoint - feedback;

    float p_term = Kp * error;

    integral += error * dt;
    float i_term = std::clamp(Ki * integral, output_min - p_term, output_max - p_term);

    float d_raw = (dt > 1e-6f) ? (error - last_error) / dt : 0.0f;
    d_filtered += filter_alpha * (d_raw - d_filtered);
    float d_term = Kd * d_filtered;

    float output = std::clamp(p_term + i_term + d_term, output_min, output_max);
    st.across[provider.get(PortNames::output)] = output;
    last_error = error;
}
```

#### Паттерн 7: Компонент с pre_load (Battery — предрасчёт)

```cpp
template <typename Provider>
void Battery<Provider>::pre_load() {
    if (internal_r > 0.0f) {
        inv_internal_r = 1.0f / internal_r;
    }
}
```

#### Паттерн 7b: pre_load вычисляет коэффициент из номинала (AZS)

```cpp
template <typename Provider>
void AZS<Provider>::pre_load() {
    if (i_nominal > 0.0f) {
        r_heat = 1.0f / (i_nominal * i_nominal);
    }
}
```

#### Паттерн 8: Нулевая реализация (Voltmeter — чисто визуальный)

```cpp
template <typename Provider>
void Voltmeter<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    // Passive — doesn't affect the circuit
}
```

#### Паттерн 9: Мульти-доменный (ElectricHeater — два solve-метода)

```cpp
template <typename Provider>
void ElectricHeater<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    float v_in = st.across[provider.get(PortNames::power)];
    float g = max_power / (v_in * v_in + 0.01f);
    st.conductance[provider.get(PortNames::power)] += g;
    st.through[provider.get(PortNames::power)] -= v_in * g * efficiency;
}

template <typename Provider>
void ElectricHeater<Provider>::solve_thermal(an24::SimulationState& st, float /*dt*/) {
    float v_in = st.across[provider.get(PortNames::power)];
    float heat_power = v_in * v_in * efficiency;
    st.through[provider.get(PortNames::heat_out)] += heat_power;
}
```

### Важные правила

1. **В `solve_xxx()` ШТАМПУЙ (accumulate) в `conductance` и `through`, ЧИТАЙ из `across`**.
2. **В `post_step()` ЧИТАЙ из `across` (результат SOR), ЗАПИСЫВАЙ в `across` (выходы)**.
3. **Никогда не обнуляй `conductance[]` или `through[]`** — симулятор обнуляет их перед каждой итерацией.
4. **Для выходного порта, который ты пишешь напрямую в `post_step()`, добавь минимальную проводимость в `solve_xxx()`**: `st.conductance[idx] += 1e-6f;`
5. **Используй `float /*dt*/`** если `dt` не используется (избегай warning).
6. **`#include <cmath>`** доступен в `all.cpp` — используй `std::clamp`, `std::abs`, `std::max`.

---

## Шаг 4: Explicit instantiation (`src/jit_solver/components/explicit_instantiations.h`)

Добавь строку В КОНЕЦ списка (перед `} // namespace an24`):

```cpp
template class ИмяКласса<JitProvider>;
```

**Это ОБЯЗАТЕЛЬНО** — без неё линкер выдаст `undefined reference to ИмяКласса<JitProvider>::...`

### Пример текущего файла (добавь в конец списка):

```cpp
namespace an24 {
template class Battery<JitProvider>;
template class Switch<JitProvider>;
// ... (45+ строк)
template class LUT<JitProvider>;
// ДОБАВЬ СЮДА:
template class ИмяКласса<JitProvider>;
} // namespace an24
```

---

## Шаг 5: Запустить codegen для port_registry.h

**НИКОГДА НЕ РЕДАКТИРУЙ `port_registry.h` ВРУЧНУЮ!**

Codegen сканирует все `library/**/*.json` файлы с `"cpp_class": true` и генерирует:

- `enum class PortNames` — имена всех уникальных портов
- `enum class ComponentType` — перечисление всех типов
- `ComponentVariant = std::variant<...>` — вариант со всеми типами
- `string_to_port_name()` — конвертация строки в enum
- `get_component_ports()` — маппинг компонент → порты

### Команда запуска

```bash
cd /Users/vladimir/an24_cpp
./build/examples/codegen_test blueprint.json /tmp/out
```

Эта команда:

1. Загружает TypeRegistry из `library/`
2. Генерирует `src/jit_solver/components/port_registry.h`
3. Генерирует AOT файлы (не критично)

### Проверить результат

```bash
grep "ИмяКласса" src/jit_solver/components/port_registry.h
```

Должны появиться:

- `ИмяКласса` в `enum class ComponentType`
- `ИмяКласса<JitProvider>` в `ComponentVariant`
- `ИмяКласса_PORT_COUNT = N`
- Имена портов в `enum class PortNames` (если они новые)

---

## Шаг 6: Добавить в фабрику (`src/jit_solver/jit_solver.cpp`)

Добавь `else if` блок в функцию `create_component_variant()`.

### Где вставить

Функция содержит цепочку `if/else if` блоков. Добавь ПЕРЕД финальным:

```cpp
    else {
        throw std::runtime_error("Unknown component type: " + dev.classname);
    }
```

### Шаблон фабричного блока

```cpp
    else if (dev.classname == "ИмяКласса") {
        ИмяКласса<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        // Установить параметры из JSON:
        comp.param1 = get_float(dev, "param1", 1.0f);    // float параметр
        comp.flag   = get_bool(dev, "flag", false);       // bool параметр
        comp.name   = get_string(dev, "name", "default"); // string параметр
        // Если есть pre_load():
        comp.pre_load();
        return ComponentVariant(std::move(comp));
    }
```

### Доступные хелперы для чтения параметров

```cpp
float       get_float(dev, "key", default_float)   // Парсит строку → float
bool        get_bool(dev, "key", default_bool)      // "true"/"1" → true, иначе false
std::string get_string(dev, "key", default_string)  // Возвращает строку
```

### `setup_component_ports(comp, dev, result)` — ОБЯЗАТЕЛЬНЫЙ вызов!

Эта функция:

1. Итерирует порты из `dev.ports`
2. Находит индекс сигнала через `result.port_to_signal`
3. Преобразует имя порта в `PortNames` enum через `string_to_port_name()`
4. Вызывает `comp.provider.set(port_enum, signal_index)`

Без этого вызова `provider.get(PortNames::xxx)` вернёт 0 для всех портов!

### Реальные примеры из фабрики

**Без параметров (Add)**:

```cpp
    else if (dev.classname == "Add") {
        Add<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        return ComponentVariant(std::move(comp));
    }
```

**С float параметрами (Resistor)**:

```cpp
    else if (dev.classname == "Resistor") {
        Resistor<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.conductance = get_float(dev, "conductance", 0.1f);
        return ComponentVariant(std::move(comp));
    }
```

**С pre_load (Battery)**:

```cpp
    else if (dev.classname == "Battery") {
        Battery<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.v_nominal = get_float(dev, "v_nominal", 28.0f);
        comp.internal_r = get_float(dev, "internal_r", 0.01f);
        comp.capacity = get_float(dev, "capacity", 1000.0f);
        comp.charge = get_float(dev, "charge", comp.capacity);
        comp.pre_load();
        return ComponentVariant(std::move(comp));
    }
```

**Мульти-доменный с pre_load (AZS)**:

```cpp
    else if (dev.classname == "AZS") {
        AZS<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.closed = get_bool(dev, "closed", false);
        comp.i_nominal = get_float(dev, "i_nominal", 20.0f);
        comp.pre_load();
        return ComponentVariant(std::move(comp));
    }
```

**С bool (Switch)**:

```cpp
    else if (dev.classname == "Switch") {
        Switch<JitProvider> comp;
        setup_component_ports(comp, dev, result);
        comp.closed = get_bool(dev, "initial_state", false);
        return ComponentVariant(std::move(comp));
    }
```

---

## Шаг 7: Собрать и проверить

```bash
cd /Users/vladimir/an24_cpp/build
make -j$(sysctl -n hw.ncpu)
ctest --output-on-failure
```

### Типичные ошибки компиляции и их решения

| Ошибка                                               | Причина                               | Решение                                             |
| ---------------------------------------------------- | ------------------------------------- | --------------------------------------------------- |
| `PortNames::xxx` не найден                           | Codegen не запущен или порт не в JSON | Запусти codegen (Шаг 5)                             |
| `undefined reference to ИмяКласса<JitProvider>::...` | Нет explicit instantiation            | Добавь строку в `explicit_instantiations.h` (Шаг 4) |
| `Unknown component type: ИмяКласса` (runtime)        | Нет в фабрике                         | Добавь `else if` в `jit_solver.cpp` (Шаг 6)         |
| `ComponentVariant` не содержит тип                   | Codegen не включил класс              | Проверь `"cpp_class": true` в JSON                  |
| `no matching function for call to solve_electrical`  | Метод объявлен, но не реализован      | Добавь реализацию в `all.cpp` (Шаг 3)               |

---

## Шаг 8 (опционально): Тесты

### Шаблон integration-теста

```cpp
#include <gtest/gtest.h>
#include "json_parser/json_parser.h"
#include "jit_solver/jit_solver.h"

using namespace an24;

TEST(MyComponentTest, BuildAndRun) {
    const char* json = R"({
        "devices": [
            {
                "name": "comp1",
                "classname": "ИмяКласса",
                "ports": {
                    "input": {"direction": "In", "type": "Any"},
                    "output": {"direction": "Out", "type": "Any"}
                },
                "params": { "param1": "2.0" }
            }
        ],
        "connections": []
    })";

    auto ctx = parse_json(json);
    std::vector<std::pair<std::string, std::string>> connections;
    for (const auto& c : ctx.connections)
        connections.push_back({c.from, c.to});

    auto result = build_systems_dev(ctx.devices, connections);
    EXPECT_NE(result.devices.find("comp1"), result.devices.end());
}
```

### Шаблон unit-теста (прямая работа с SimulationState)

```cpp
#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/state.h"

using namespace an24;

TEST(MyComponentTest, BasicBehavior) {
    ИмяКласса<JitProvider> comp;
    comp.provider.set(PortNames::input, 0);
    comp.provider.set(PortNames::output, 1);
    comp.param1 = 2.0f;

    SimulationState st;
    st.across.resize(4, 0.0f);
    st.through.resize(4, 0.0f);
    st.conductance.resize(4, 0.0f);

    // Задать вход
    st.across[0] = 10.0f;

    // Выполнить шаг
    comp.solve_logical(st, 1.0f / 60.0f);

    // Проверить результат
    EXPECT_NEAR(st.across[1], 20.0f, 0.01f);
}
```

### CMake для теста

Добавь в `tests/CMakeLists.txt`:

```cmake
add_executable(my_component_tests
    test_my_component.cpp
)
target_include_directories(my_component_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(my_component_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(my_component_tests)
```

---

## Шаг 9 (опционально): Оптимизации компонентов

В этом проекте используются несколько паттернов оптимизации для максимальной производительности и WASM-совместимости. Все оптимизации направлены на:

- **Устранение ветвлений** в hot path (branchless programming)
- **Предвычисление** делений в `pre_load()`
- **Защиту от паузы** (pause-safe) через централизованный clamp
- **Защиту от дребезга** (deadzone) для стабилизации выходов

### 9.1 Pause-Safe компоненты

**Проблема**: Симуляция может быть поставлена на паузу (`dt = 0`), что приводит к:

- Делению на ноль в производных `d/dt ≈ Δ/dt`
- Численной нестабильности при экстремальных значениях `dt`

**Решение**: Централизованный clamp в `systems.cpp` — **ЕДИНСТВЕННОЕ МЕСТО** для ограничения `dt`:

```cpp
// src/jit_solver/systems.cpp
void Systems::solve_step(SimulationState& state, size_t step, float dt) {
    // Pause-safe and lag-spike protection: clamp dt ONCE for all domains
    constexpr float DT_MIN = 1e-6f;  // Prevent div-by-zero
    constexpr float DT_MAX = 0.1f;   // Prevent instability on lag spikes
    float safe_dt = std::max(DT_MIN, std::min(dt, DT_MAX));

    // Все компоненты получают пред-ограниченный dt:
    for (auto& comp : electrical) {
        comp->solve_electrical(state, safe_dt);
    }
    // ...
}
```

**ВАЖНО**: Не клампай `dt` в каждом компоненте! Это экономит десятки вызовов `std::min/max` на кадр.

**Исключение**: Если компонент тестируется напрямую через unit-тесты (минуя `systems.cpp`), добавь локальный clamp:

```cpp
// Self-contained dt clamping for testability (core also clamps, defense in depth)
float safe_dt = std::max(1e-6f, std::min(dt, 0.1f));
```

### 9.2 Branchless programming — безусловное выполнение

WASM и современные CPU плохо предсказывают ветвления. Заменяем `if/else` на float-маски.

#### Паттерн 1: Маска для deadzone

Вместо:

```cpp
if (std::abs(diff) >= deadzone) {
    current_value += diff * factor;
}
```

Используем:

```cpp
float dz_mask = (std::abs(diff) >= deadzone) ? 1.0f : 0.0f;
current_value += diff * factor * dz_mask;
```

**Пример: FastTMO**

```cpp
template <typename Provider>
void FastTMO<Provider>::solve_logical(an24::SimulationState& st, float dt) {
    uint32_t in_idx = provider.get(PortNames::in);
    uint32_t out_idx = provider.get(PortNames::out);
    float input = st.across[in_idx];

    // Первичный кадр: инициализация current_value = input
    current_value += (input - current_value) * first_frame_mask;
    first_frame_mask = 0.0f;

    float diff = input - current_value;
    float factor = std::min(dt * inv_tau, 1.0f);
    float dz_mask = (std::abs(diff) >= deadzone) ? 1.0f : 0.0f;

    current_value += diff * factor * dz_mask;
    st.across[out_idx] = current_value;
}
```

**JSON для FastTMO** (`library/math/FastTMO.json`):

```json
{
  "classname": "FastTMO",
  "description": "WASM-optimized branchless TMO filter with rational approximation",
  "cpp_class": true,
  "ports": {
    "in": { "direction": "In", "type": "Any" },
    "out": { "direction": "Out", "type": "Any" }
  },
  "params": {
    "tau": "0.1",
    "deadzone": "0.001"
  },
  "domains": ["Logical"],
  "priority": "med",
  "critical": false
}
```

**all.h — объявление**:

```cpp
template <typename Provider = JitProvider>
class FastTMO {
public:
    static constexpr Domain domain = Domain::Logical;
    Provider provider;

    // Параметры
    float tau = 0.1f;
    float deadzone = 0.001f;

    // Внутреннее состояние
    float current_value = 0.0f;
    float first_frame_mask = 1.0f;  // 1.0 на первом кадре, 0.0 потом

    // Предвычисленные значения
    float inv_tau = 10.0f;  // = 1/tau, вычисляется в pre_load()

    FastTMO() = default;
    void solve_logical(an24::SimulationState& st, float dt);
    void pre_load();
};
```

**all.cpp — реализация**:

```cpp
template <typename Provider>
void FastTMO<Provider>::pre_load() {
    inv_tau = 1.0f / std::max(tau, 0.0001f);
}

template <typename Provider>
void FastTMO<Provider>::solve_logical(an24::SimulationState& st, float dt) {
    uint32_t in_idx = provider.get(PortNames::in);
    uint32_t out_idx = provider.get(PortNames::out);
    float input = st.across[in_idx];

    // Первичный кадр: инициализация current_value = input
    current_value += (input - current_value) * first_frame_mask;
    first_frame_mask = 0.0f;

    float diff = input - current_value;
    float factor = std::min(dt * inv_tau, 1.0f);
    float dz_mask = (std::abs(diff) >= deadzone) ? 1.0f : 0.0f;

    current_value += diff * factor * dz_mask;
    st.across[out_idx] = current_value;
}
```

#### Паттерн 2: XOR через float-маски

Вместо:

```cpp
if (normally_closed) {
    open = (ctrl <= 12.0f);
} else {
    open = (ctrl > 12.0f);
}
```

Используем:

```cpp
float ctrl_above = (ctrl > 12.0f) ? 1.0f : 0.0f;
float nc_mask = normally_closed ? 1.0f : 0.0f;
open_mask = std::abs(ctrl_above - nc_mask);  // XOR: 0⊕0=0, 0⊕1=1, 1⊕0=1, 1⊕1=0
```

**Пример: SolenoidValve**

```cpp
template <typename Provider>
void SolenoidValve<Provider>::solve_hydraulic(an24::SimulationState& st, float /*dt*/) {
    float ctrl = st.across[provider.get(PortNames::ctrl)];

    float ctrl_above = (ctrl > 12.0f) ? 1.0f : 0.0f;
    float nc_mask = normally_closed ? 1.0f : 0.0f;
    open_mask = std::abs(ctrl_above - nc_mask);  // XOR

    float g = 1.0e6f * open_mask;
    st.conductance[provider.get(PortNames::flow_in)] += g;
    st.conductance[provider.get(PortNames::flow_out)] += g;
}
```

#### Паттерн 3: Branchless guard для деления

Вместо:

```cpp
if (v_diff > min_voltage_diff) {
    float i = power_draw / v_diff;
    float g = i / v_diff;
    // ...
}
```

Используем:

```cpp
float safe_v_diff = std::max(v_diff, min_voltage_diff);
float conduct_mask = (v_diff > min_voltage_diff) ? 1.0f : 0.0f;

float i = power_draw / safe_v_diff * conduct_mask;
float g = i / safe_v_diff;
// i=0, g=0 если conduct_mask=0 (защита от деления на ноль)
```

**Пример: HighPowerLoad**

```cpp
template <typename Provider>
void HighPowerLoad<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    float v_in = st.across[provider.get(PortNames::v_in)];
    float v_out = st.across[provider.get(PortNames::v_out)];
    float v_diff = v_in - v_out;

    float safe_v_diff = std::max(v_diff, min_voltage_diff);
    float conduct_mask = (v_diff > min_voltage_diff) ? 1.0f : 0.0f;

    float i = power_draw / safe_v_diff * conduct_mask;
    float g = i / safe_v_diff;

    st.through[provider.get(PortNames::v_out)] += i;
    st.through[provider.get(PortNames::v_in)] -= i;
    st.conductance[provider.get(PortNames::v_out)] += g;
    st.conductance[provider.get(PortNames::v_in)] += g;
}
```

**JSON для HighPowerLoad** (`library/electrical/HighPowerLoad.json`):

```json
{
  "classname": "HighPowerLoad",
  "description": "Constant power load (adjusts conductance based on voltage, branchless optimized)",
  "cpp_class": true,
  "ports": {
    "v_in": { "direction": "In", "type": "V" },
    "v_out": { "direction": "Out", "type": "V" }
  },
  "params": {
    "power_draw": "500.0",
    "min_voltage_diff": "0.01"
  },
  "domains": ["Electrical"],
  "priority": "med",
  "critical": false
}
```

### 9.3 Предвычисление инверсий

Деление — ~10-20x медленнее умножения. Переносим деления из hot path в `pre_load()`.

#### Паттерн: `inv_x = 1.0f / x`

**all.h**:

```cpp
float internal_r = 0.01f;    // Параметр из JSON
float inv_internal_r = 100.0f;  // Предвычисленное, = 1/internal_r

void pre_load();  // Объявление
```

**all.cpp**:

```cpp
template <typename Provider>
void Generator<Provider>::pre_load() {
    inv_internal_r = (internal_r > 0.0f) ? (1.0f / internal_r) : 0.0f;
}

template <typename Provider>
void Generator<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    // Было: float g = 1.0f / internal_r;
    // Стало:
    float g = inv_internal_r;  // Предвычисленное, без деления
    // ...
}
```

### 9.4 Deadzone — зона нечувствительности

Компоненты с feedback loop (TMO, PID, Lerp) могут "дребезжать" около setpoint из-за численной погрешности. Deadzone отсекает микро-коррекции.

**Когда добавлять deadzone**:

- Компонент имеет внутреннее состояние (`current_value`, `integral`, и т.д.)
- Вычисляет разницу `diff = target - current`
- Применяет коррекцию proportional к `diff`

**Типичные значения deadzone**:

- Для напряжений (0-30V): `0.001` — `0.01`
- Для нормированных сигналов (0-1): `0.0001` — `0.001`
- Для температур: `0.1` — `1.0`

**Пример: LerpNode с deadzone**

```cpp
// all.h
template <typename Provider = JitProvider>
class LerpNode {
public:
    static constexpr Domain domain = Domain::Logical;
    Provider provider;
    float factor = 0.05f;
    float deadzone = 0.001f;  // Новый параметр

    // Внутреннее состояние
    float current_value = 0.0f;
    float first_frame_mask = 1.0f;

    LerpNode() = default;
    void post_step(an24::SimulationState& st, float dt);
    void pre_load() {}
};

// all.cpp
template <typename Provider>
void LerpNode<Provider>::post_step(an24::SimulationState& st, float dt) {
    (void)dt;
    float v_input = st.across[provider.get(PortNames::input)];

    // Первичный кадр: инициализация
    current_value += (v_input - current_value) * first_frame_mask;
    first_frame_mask = 0.0f;

    float diff = v_input - current_value;
    float dz_mask = (std::abs(diff) >= deadzone) ? 1.0f : 0.0f;

    float new_output = current_value + factor * diff * dz_mask;
    current_value = new_output;
    st.across[provider.get(PortNames::output)] = new_output;
}
```

**JSON для LerpNode** (`library/math/LerpNode.json`):

```json
{
  "classname": "LerpNode",
  "description": "Linear interpolation node (first-order filter) for sensor simulation with deadzone",
  "cpp_class": true,
  "ports": {
    "input": { "direction": "In", "type": "Any" },
    "output": { "direction": "Out", "type": "Any" }
  },
  "params": {
    "factor": "0.05",
    "deadzone": "0.001"
  },
  "domains": ["Logical"],
  "priority": "med",
  "critical": false
}
```

### 9.5 First frame initialization — холодный старт без if

Компоненты с внутренним состоянием должны инициализироваться первым входным значением. Вместо `if (first_frame) { ... }` используем маску.

**Паттерн**:

```cpp
// В all.h:
float current_value = 0.0f;
float first_frame_mask = 1.0f;  // 1.0 → 0.0 после первого кадра

// В solve/post_step:
// current_value = input на первом кадре, потом current_value += ...
current_value += (input - current_value) * first_frame_mask;
first_frame_mask = 0.0f;  // После первого кадра больше не влияет
```

**Почему это работает**:

- Кадр 1: `current_value += (input - 0) * 1.0` → `current_value = input`
- Кадр 2+: `current_value += (input - current_value) * 0.0` → `current_value` не меняется этой строкой

### 9.6 Полные примеры оптимизированных компонентов

#### AsymTMO — асимметричный фильтр

**library/math/AsymTMO.json**:

```json
{
  "classname": "AsymTMO",
  "description": "Asymmetric TMO filter (different rise/fall rates)",
  "cpp_class": true,
  "ports": {
    "in": { "direction": "In", "type": "Any" },
    "out": { "direction": "Out", "type": "Any" }
  },
  "params": {
    "tau_up": "0.1",
    "tau_down": "0.5",
    "deadzone": "0.001"
  },
  "domains": ["Logical"],
  "priority": "med",
  "critical": false
}
```

**all.h**:

```cpp
template <typename Provider = JitProvider>
class AsymTMO {
public:
    static constexpr Domain domain = Domain::Logical;
    Provider provider;

    // Параметры
    float tau_up = 0.1f;
    float tau_down = 0.5f;
    float deadzone = 0.001f;

    // Внутреннее состояние
    float current_value = 0.0f;
    float first_frame_mask = 1.0f;

    // Предвычисленные
    float inv_tau_up = 10.0f;
    float inv_tau_down = 2.0f;

    AsymTMO() = default;
    void solve_logical(an24::SimulationState& st, float dt);
    void pre_load();
};
```

**all.cpp**:

```cpp
template <typename Provider>
void AsymTMO<Provider>::pre_load() {
    inv_tau_up = 1.0f / std::max(tau_up, 0.0001f);
    inv_tau_down = 1.0f / std::max(tau_down, 0.0001f);
}

template <typename Provider>
void AsymTMO<Provider>::solve_logical(an24::SimulationState& st, float dt) {
    uint32_t in_idx = provider.get(PortNames::in);
    uint32_t out_idx = provider.get(PortNames::out);
    float input = st.across[in_idx];

    current_value += (input - current_value) * first_frame_mask;
    first_frame_mask = 0.0f;

    float diff = input - current_value;
    float active_inv_tau = (diff > 0.0f) ? inv_tau_up : inv_tau_down;
    float factor = std::min(dt * active_inv_tau, 1.0f);
    float dz_mask = (std::abs(diff) >= deadzone) ? 1.0f : 0.0f;

    current_value += diff * factor * dz_mask;
    st.across[out_idx] = current_value;
}
```

#### SlewRate — линейный ограничитель скорости

**library/math/SlewRate.json**:

```json
{
  "classname": "SlewRate",
  "description": "Linear rate limiter (slew rate limiter)",
  "cpp_class": true,
  "ports": {
    "in": { "direction": "In", "type": "Any" },
    "out": { "direction": "Out", "type": "Any" }
  },
  "params": {
    "max_rate": "1.0",
    "deadzone": "0.0001"
  },
  "domains": ["Logical"],
  "priority": "med",
  "critical": false
}
```

**all.h**:

```cpp
template <typename Provider = JitProvider>
class SlewRate {
public:
    static constexpr Domain domain = Domain::Logical;
    Provider provider;

    // Параметры
    float max_rate = 1.0f;
    float deadzone = 0.0001f;

    // Внутреннее состояние
    float current_value = 0.0f;
    float first_frame_mask = 1.0f;

    SlewRate() = default;
    void solve_logical(an24::SimulationState& st, float dt);
    void pre_load() {}
};
```

**all.cpp**:

```cpp
template <typename Provider>
void SlewRate<Provider>::solve_logical(an24::SimulationState& st, float dt) {
    uint32_t in_idx = provider.get(PortNames::in);
    uint32_t out_idx = provider.get(PortNames::out);
    float input = st.across[in_idx];

    current_value += (input - current_value) * first_frame_mask;
    first_frame_mask = 0.0f;

    float diff = input - current_value;
    float max_step = max_rate * dt;
    float limited_diff = std::max(-max_step, std::min(max_step, diff));
    float dz_mask = (std::abs(diff) >= deadzone) ? 1.0f : 0.0f;

    current_value += limited_diff * dz_mask;
    st.across[out_idx] = current_value;
}
```

### 9.7 WASM friendliness — чеклист

WASM (WebAssembly) имеет особенности производительности:

| Паттерн                      | WASM-дружелюбность | Почему                               |
| ---------------------------- | ------------------ | ------------------------------------ |
| **Branchless с float masks** | ✅ Отлично         | Нет branch prediction penalties      |
| **Предвычисленные инверсии** | ✅ Отлично         | Деление дорого в WASM                |
| **Deadzone**                 | ✅ Отлично         | Уменьшает дребезг, меньше обновлений |
| **First frame masks**        | ✅ Отлично         | Нет `if` на каждый кадр              |
| **`expf`, `logf`, `powf`**   | ⚠️ Медленно        | Используй рациональные аппроксимации |
| **`std::sin`, `std::cos`**   | ⚠️ Медленно        | LUT или lookup table                 |
| **`if/else` в hot path**     | ❌ Плохо           | Branch misprediction ~15-20 cycles   |
| **Деление в hot path**       | ❌ Плохо           | ~20x медленнее умножения             |

### 9.8 Резюме: что оптимизировать в первую очередь

1. **Высокий приоритет** (деления в hot path):
   - Перенеси все `1.0f / x` в `pre_load()`
   - Пример: `Generator::inv_internal_r`, `Battery::inv_internal_r`

2. **Высокий приоритет** (ветвления в hot path):
   - Замени `if (abs(x) > threshold)` на mask-based logic
   - Пример: `FastTMO`, `AsymTMO`, `SlewRate`, `HighPowerLoad`

3. **Средний приоритет** (стабильность):
   - Добавь deadzone для компонентов с feedback
   - Пример: `LerpNode`, `FastTMO`, `AsymTMO`

4. **Низкий приоритет** ( readability vs performance ):
   - Если компонент вызывается редко (Thermal domain, 1 Hz),
     ветвления допустимы для ясности кода

---

## Шаг 10: Известные ловушки (из code review 2026-03-10)

Эти ловушки были обнаружены при ревью 5 компонентов (AsymSlewRate, TimeDelay, Monostable, SampleHold, Integrator). Запомни их — они применимы ко ВСЕМ будущим компонентам.

### 10.1 Cold start: синхронизация `last_in` через `first_frame_mask`

**Проблема**: Компоненты с детектором фронта (`last_in`) и аккумулятором часто содержат баг — `last_in = raw_in` выполняется каждый кадр ДО сравнения `(raw_in == last_in)`, из-за чего аккумулятор никогда не сбрасывается при изменении входа.

**Пример бага (TimeDelay)**:

```cpp
// ❌ НЕПРАВИЛЬНО: last_in обновляется каждый кадр,
//    accumulator = (raw_in == last_in) ВСЕГДА true
current_out += (raw_in - current_out) * first_frame_mask;
last_in = raw_in;  // ← БАГ: убивает сравнение ниже
first_frame_mask = 0.0f;

accumulator = (raw_in == last_in) ? (accumulator + dt) : 0.0f;  // ← always true
last_in = raw_in;
```

**Правильный паттерн**:

```cpp
// ✅ ПРАВИЛЬНО: last_in синхронизируется ТОЛЬКО на первом кадре через маску
current_out += (raw_in - current_out) * first_frame_mask;
last_in += (raw_in - last_in) * first_frame_mask;  // Синхронизация только на cold start
first_frame_mask = 0.0f;

accumulator = (raw_in == last_in) ? (accumulator + dt) : 0.0f;
last_in = raw_in;  // ← Обновление ПОСЛЕ сравнения — корректно
```

**Правило**: Если компонент имеет и `first_frame_mask`, и `last_in`/`last_trig`:

- Синхронизируй `last_in` через маску: `last_in += (raw_in - last_in) * first_frame_mask;`
- НЕ перезаписывай `last_in = raw_in` до того, как используешь `last_in` в сравнении

### 10.2 FP drift при sequential accumulation

**Проблема**: Сложение `1.0f/60.0f` в цикле 60 раз НЕ даёт ровно `1.0f` — FP drift делает результат чуть меньше. Поэтому `accumulator >= target_delay` может не сработать на 60-м кадре, а только на 61-м.

**Влияет на**: TimeDelay, Monostable, любой компонент с таймером.

**Правило для тестов**: При проверке таймеров, добавляй +1–2 кадра запас сверх теоретического:

```cpp
// Теория: 0.5s @ 60Hz = 30 кадров
// Практика: нужно 31-32 кадра из-за FP drift
for (int i = 0; i < 32; ++i) {   // НЕ 30
    comp.solve_logical(st, 1.0f / 60.0f);
}
EXPECT_FLOAT_EQ(st.across[1], 1.0f);
```

**Альтернатива**: Использовать крупный dt для точных проверок:

```cpp
comp.solve_logical(st, 0.5f);  // Один шаг = точное значение, без FP drift
EXPECT_FLOAT_EQ(comp.accumulator, 0.5f);
```

### 10.3 "Reset frame" — кадр сброса аккумулятора

**Проблема**: Когда вход меняется (например, 0→1), первый кадр после изменения — это кадр сброса аккумулятора (`accumulator = 0.0f`). Полезная аккумуляция начинается только со СЛЕДУЮЩЕГО кадра.

**Пример**: TimeDelay с `delay_on = 0.5s`:

```
Кадр 0: input=0, output=0, accumulator=X  (стабильное состояние)
Кадр 1: input=1, accumulator=0.0         (reset frame — БЕЗ аккумуляции)
Кадр 2: input=1, accumulator=1/60        (первый кадр аккумуляции)
...
Кадр 31: input=1, accumulator≈0.5        (delay expired)
```

**Правило для тестов**: Общее число кадров = `ceil(delay / dt) + 1` (reset frame) + 1 (FP margin).

### 10.4 Rate vs. step-per-frame в тестах

**Проблема**: Rate-limited компоненты (SlewRate, AsymSlewRate) двигают значение на `rate * dt` за кадр. Если `rate = 100` и `dt = 1/60`, шаг = 1.67 за кадр, НЕ 100.

Тест вида:

```cpp
// ❌ rate_up = 100, dt = 1/60 → шаг = 1.67, после 1 кадра будет 1.67, а НЕ 10.0
comp.solve_logical(st, 1.0f / 60.0f);
EXPECT_NEAR(st.across[1], 10.0f, 0.5f);  // FAIL
```

**Правило**: Для "мгновенной" конвергенции в 1 кадр, rate должен быть ≥ `target / dt`:

```cpp
// ✅ rate_up = 1000, dt = 1/60 → шаг = 16.67, target = 10 → достигнет за 1 кадр
auto comp = make_asym_slew_rate(1000.0f, 2.0f);
```

### 10.5 Integrator: reset vs. initial_val

**Нюанс**: Reset обнуляет аккумулятор в `0.0f`, а НЕ в `initial_val`. Это намеренное поведение — reset = "очистить накопленное", а не "вернуть к начальному".

```cpp
accumulator = (reset_in > 0.5f) ? 0.0f : accumulator;  // Reset → 0, не initial_val
```

Если нужен reset в `initial_val` — это другой компонент (или другой параметр `reset_to`).

### 10.6 Чеклист для тестов компонентов с состоянием

При написании тестов для компонентов с `first_frame_mask`, `last_in`, `accumulator`, `timer`:

- [ ] **Cold start**: первый кадр корректно инициализирует состояние
- [ ] **Pause (dt=0)**: состояние не меняется при dt=0
- [ ] **Variable dt**: компонент адаптирует шаг пропорционально dt
- [ ] **Reset frame**: учтён кадр сброса при изменении входа
- [ ] **FP margin**: учтён дрифт при sequential accumulation (~+1-2 кадра)
- [ ] **Rate calibration**: `rate * dt` даёт ожидаемый шаг при текущем dt теста
- [ ] **Edge detection**: rising edge, falling edge, sustained high/low
- [ ] **Multiple cycles**: повторные ON/OFF не наследуют состояние предыдущего цикла
- [ ] **Extreme values**: negative inputs, zero inputs, very large inputs

---

## Чеклист

- [ ] `library/<category>/<Name>.json` создан с `"cpp_class": true`
- [ ] Порты в JSON совпадают с `PortNames::xxx` в коде
- [ ] Параметры в JSON совпадают с полями класса
- [ ] Класс объявлен в `all.h` с `Provider provider` и `static constexpr Domain domain`
- [ ] Реализация в `all.cpp` с `template <typename Provider>`
- [ ] `template class ИмяКласса<JitProvider>;` в `explicit_instantiations.h`
- [ ] Codegen запущен → `port_registry.h` обновлён
- [ ] `else if (dev.classname == "ИмяКласса")` добавлен в фабрику `jit_solver.cpp`
- [ ] `setup_component_ports(comp, dev, result)` вызван в фабрике
- [ ] `comp.pre_load()` вызван если есть `pre_load()`
- [ ] Проект собирается без ошибок
- [ ] Тесты проходят

---

## Частые ошибки

1. **Забыл `"cpp_class": true` в JSON** → codegen не включит в `ComponentVariant`, фабрика бросит `throw`
2. **Забыл запустить codegen** → `PortNames::xxx` не компилируется
3. **Забыл explicit instantiation** → linker error: `undefined reference`
4. **Забыл `else if` в фабрике** → runtime throw: "Unknown component type"
5. **Забыл `setup_component_ports()`** → все порты маппятся на индекс 0, мусор в симуляции
6. **Редактировал `port_registry.h` вручную** → изменения пропадут при codegen
7. **Нет stamping в `solve_xxx()` для выходного порта** → SOR не "видит" узел, напряжение = 0
8. **Имя порта в JSON ≠ имени в коде** → `string_to_port_name()` вернёт `nullopt`, `abort()` в debug
9. **Домен в JSON не совпадает с `domain` в классе** → компонент добавлен не в тот scheduling вектор
