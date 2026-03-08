# Инструкция: Добавление нового компонента

Пошаговая инструкция для добавления нового компонента в симулятор АН-24.
Компонент должен работать и в JIT (runtime), и в AOT (compile-time) режимах через шаблонный Provider-паттерн.

---

## Обзор файлов, которые нужно изменить

| #   | Файл                                                  | Действие                                                   |
| --- | ----------------------------------------------------- | ---------------------------------------------------------- |
| 1   | `components/<Name>.json`                              | **Создать** — JSON-определение компонента                  |
| 2   | `src/jit_solver/components/all.h`                     | **Добавить** — объявление шаблонного класса                |
| 3   | `src/jit_solver/components/all.cpp`                   | **Добавить** — реализацию методов                          |
| 4   | `src/jit_solver/components/explicit_instantiations.h` | **Добавить** — строку `template class Name<JitProvider>;`  |
| 5   | `src/jit_solver/components/port_registry.h`           | **НЕ ТРОГАТЬ** — генерируется автоматически codegen        |
| 6   | `src/jit_solver/jit_solver.cpp`                       | **Добавить** — case в фабрику `create_component_variant()` |
| 7   | `tests/test_<name>.cpp`                               | **Создать** — юнит-тесты                                   |
| 8   | `tests/CMakeLists.txt`                                | **Добавить** — тест-таргет                                 |

---

## Шаг 1: JSON-определение (`components/<Name>.json`)

Создать файл `components/<Name>.json`. Codegen читает все файлы из `components/` и генерирует `port_registry.h`.

### Формат

```json
{
  "classname": "MyComponent",
  "description": "Описание компонента",
  "default_ports": {
    "port_name": {
      "direction": "In" | "Out | InOut",
      "type": "<PortType>"
    }
  },
  "default_params": {
    "param_name": "default_value_string"
  },
  "default_domains": ["<Domain>"],
  "default_priority": "low" | "med" | "high",
  "default_critical": false,
  "default_content_type": "MyComponent"
}
```

### Допустимые значения

**direction**: `"In"`, `"Out"`

**type** (тип порта): `"V"`, `"I"`, `"Bool"`, `"Temperature"`, `"Pressure"`, `"RPM"`, `"Any"`

**domains**: `"Electrical"`, `"Logical"`, `"Mechanical"`, `"Hydraulic"`, `"Thermal"`

Домены определяют частоту обновления:

- Electrical / Logical — 60 Hz
- Mechanical — 20 Hz
- Hydraulic — 5 Hz
- Thermal — 1 Hz

### Пример (PID-регулятор)

```json
{
  "classname": "PID",
  "description": "PID controller with derivative filter and output clamping",
  "default_ports": {
    "feedback": { "direction": "In", "type": "Any" },
    "setpoint": { "direction": "In", "type": "Any" },
    "output": { "direction": "Out", "type": "Any" }
  },
  "default_params": {
    "Kp": "1.0",
    "Ki": "0.0",
    "Kd": "0.0",
    "output_min": "-1000.0",
    "output_max": "1000.0",
    "filter_alpha": "0.2"
  },
  "default_domains": ["Electrical"],
  "default_priority": "med",
  "default_critical": false,
  "default_content_type": "PID"
}
```

---

## Шаг 2: Объявление класса (`src/jit_solver/components/all.h`)

Добавить шаблонный класс в `all.h` внутри `namespace an24`.

### Шаблон

```cpp
/// Краткое описание компонента
template <typename Provider = JitProvider>
class MyComponent {
public:
    static constexpr Domain domain = Domain::Electrical; // выбрать нужный домен

    Provider provider;

    // Параметры (из default_params JSON)
    float param1 = 1.0f;
    float param2 = 0.0f;

    // Внутреннее состояние (если нужно)
    float state_var = 0.0f;

    MyComponent() = default;

    // Методы (объявить только нужные, реализация в all.cpp)
    void solve_electrical(an24::SimulationState& st, float dt);
    void post_step(an24::SimulationState& st, float dt);
};
```

### Доступные методы для реализации

| Метод                      | Когда вызывается                    | Типичное использование                                                  |
| -------------------------- | ----------------------------------- | ----------------------------------------------------------------------- |
| `solve_electrical(st, dt)` | Каждый шаг — фаза штамповки матрицы | Штамповать проводимость/ток в `st.conductance[]`, `st.through[]`        |
| `post_step(st, dt)`        | После решения SOR — фаза обновления | Читать `st.across[]`, обновлять внутреннее состояние, записывать выходы |
| `pre_load()`               | Один раз при инициализации          | Предрасчёт (например, `inv_r = 1/r`)                                    |

### Важно

- **`Provider provider`** — обязательное поле. Через `provider.get(PortNames::xxx)` получаем индексы портов в `SimulationState`.
- **`static constexpr Domain domain`** — обязательно. Определяет частоту обновления. Можно комбинировать: `Domain::Electrical | Domain::Thermal`.
- Имя порта (например `feedback`) должно соответствовать `PortNames::feedback` — это enum, генерируемый codegen из JSON.

---

## Шаг 3: Реализация (`src/jit_solver/components/all.cpp`)

Добавить реализацию методов в `all.cpp`.

### SimulationState — работа с массивами

```
st.across[idx]       — потенциал / напряжение на узле (читать в post_step)
st.through[idx]      — ток / поток (штамповать в solve_electrical)
st.conductance[idx]  — проводимость узла (штамповать в solve_electrical)
```

Индекс `idx` получаем через `provider.get(PortNames::port_name)`.

### Stamp-хелперы (определены в `state.h`)

```cpp
// Двухпортовый — проводимость между двумя узлами
stamp_two_port(st.conductance.data(), st.through.data(), st.across.data(),
               idx1, idx2, g);

// Однопортовый на землю — нагрузка
stamp_one_port_ground(st.conductance.data(), st.through.data(), st.across.data(),
                      idx, g);

// Источник тока (Norton)
stamp_current_source(st.conductance.data(), st.through.data(),
                     idx, g, i_source);

// Источник напряжения (Thevenin → Norton)
stamp_voltage_source(st.conductance.data(), st.through.data(), st.across.data(),
                     idx, v_source, r_internal);
```

### Пример: PID-регулятор

```cpp
template <typename Provider>
void PID<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    // Минимальная проводимость на выходе — чтобы SOR "видел" узел
    float g = 1e-6f;
    stamp_one_port_ground(st.conductance.data(), st.through.data(), st.across.data(),
                          provider.get(PortNames::output), g);
}

template <typename Provider>
void PID<Provider>::post_step(an24::SimulationState& st, float dt) {
    float fb = st.across[provider.get(PortNames::feedback)];
    float sp = st.across[provider.get(PortNames::setpoint)];

    float error = sp - fb;

    // P
    float p_term = Kp * error;

    // I (с anti-windup)
    integral += error * dt;
    float i_term = Ki * integral;

    // D (с фильтрованной производной)
    float raw_d = (dt > 0.0f) ? (error - last_error) / dt : 0.0f;
    d_filtered = d_filtered + filter_alpha * (raw_d - d_filtered);
    float d_term = Kd * d_filtered;

    last_error = error;

    float out = std::clamp(p_term + i_term + d_term, output_min, output_max);

    // Anti-windup: откатить интеграл если выход насыщен
    if (out != p_term + i_term + d_term) {
        integral -= error * dt;
    }

    st.across[provider.get(PortNames::output)] = out;
}
```

### Паттерн для простых компонентов (только пропуск сигнала)

```cpp
template <typename Provider>
void MyComponent<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    float g = 1e-6f;
    stamp_one_port_ground(st.conductance.data(), st.through.data(), st.across.data(),
                          provider.get(PortNames::output), g);
}

template <typename Provider>
void MyComponent<Provider>::post_step(an24::SimulationState& st, float /*dt*/) {
    float input = st.across[provider.get(PortNames::input)];
    st.across[provider.get(PortNames::output)] = input * factor;
}
```

### Паттерн для электрических компонентов (резистор, нагрузка)

```cpp
template <typename Provider>
void MyComponent<Provider>::solve_electrical(an24::SimulationState& st, float /*dt*/) {
    stamp_two_port(st.conductance.data(), st.through.data(), st.across.data(),
                   provider.get(PortNames::v_in), provider.get(PortNames::v_out), conductance);
}
```

---

## Шаг 4: Explicit instantiation (`src/jit_solver/components/explicit_instantiations.h`)

Добавить строку в блок `namespace an24`:

```cpp
template class MyComponent<JitProvider>;
```

**Это обязательно** — без неё линкер не найдёт символы шаблонного класса при сборке `libjit_solver.a`.

---

## Шаг 5: Запустить codegen

**НИКОГДА не редактировать `port_registry.h` вручную!**

Codegen читает все `components/*.json` и генерирует `src/jit_solver/components/port_registry.h`, который содержит:

- `enum class PortNames` — имена всех портов всех компонентов
- `enum class ComponentType` — перечисление типов компонентов
- `using ComponentVariant = std::variant<...>` — вариант со всеми типами
- `get_component_ports()` — маппинг компонент → порты

### Команда

```bash
cd /Users/vladimir/an24_cpp
cmake --build build --target codegen_test
./build/examples/codegen_test blueprint.json /tmp/out
```

После запуска проверить, что новый компонент появился в `port_registry.h`:

```bash
grep "MyComponent" src/jit_solver/components/port_registry.h
```

---

## Шаг 6: Фабрика (`src/jit_solver/jit_solver.cpp`)

Добавить `else if` блок в функцию `create_component_variant()` (перед финальным `else { throw ... }`).

### Шаблон

```cpp
else if (dev.classname == "MyComponent") {
    MyComponent<JitProvider> comp;
    setup_component_ports(comp, dev, result);
    // Установить параметры из JSON blueprint:
    comp.param1 = get_float(dev, "param1", 1.0f);    // float
    comp.param2 = get_bool(dev, "param2", false);      // bool
    comp.name   = get_string(dev, "name", "default");  // string
    // Если есть pre_load():
    comp.pre_load();
    return ComponentVariant(std::move(comp));
}
```

### Доступные хелперы для чтения параметров

```cpp
float  get_float(const DeviceInstance& dev, const std::string& key, float default_val);
bool   get_bool(const DeviceInstance& dev, const std::string& key, bool default_val);
std::string get_string(const DeviceInstance& dev, const std::string& key, const std::string& default_val);
```

`setup_component_ports(comp, dev, result)` — обязательный вызов, привязывает порты компонента к индексам в `SimulationState`.

---

## Шаг 7: Тесты (`tests/test_<name>.cpp`)

### Шаблон теста

```cpp
#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/all.cpp"  // нужно для template definitions
#include "jit_solver/components/port_registry.h"

using namespace an24;

// Хелпер: создать компонент с привязанными портами
static MyComponent<JitProvider> make_my_component() {
    MyComponent<JitProvider> comp;
    // Привязать порты к индексам SimulationState
    // Порядок индексов — произвольный, главное не перекрываются
    comp.provider.port_map[static_cast<uint32_t>(PortNames::input)]  = 0;
    comp.provider.port_map[static_cast<uint32_t>(PortNames::output)] = 1;
    // Установить параметры
    comp.param1 = 1.0f;
    return comp;
}

// Хелпер: создать SimulationState нужного размера
static SimulationState make_state(size_t n = 4) {
    SimulationState st;
    st.across.resize(n, 0.0f);
    st.through.resize(n, 0.0f);
    st.conductance.resize(n, 0.0f);
    return st;
}

TEST(MyComponentTest, BasicOutput) {
    auto comp = make_my_component();
    auto st = make_state();

    // Задать входы
    st.across[0] = 10.0f;  // input

    // Выполнить шаг
    comp.solve_electrical(st, 1.0f / 60.0f);
    comp.post_step(st, 1.0f / 60.0f);

    // Проверить выход
    EXPECT_NEAR(st.across[1], 10.0f, 0.01f);  // output
}

TEST(MyComponentTest, ZeroInput) {
    auto comp = make_my_component();
    auto st = make_state();
    comp.solve_electrical(st, 1.0f / 60.0f);
    comp.post_step(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 0.0f, 0.01f);
}
```

### Что нужно протестировать

1. Базовое поведение (корректный выход при известном входе)
2. Нулевой вход (поведение при отсутствии сигнала)
3. Граничные значения (насыщение, максимумы)
4. Стабильность при многократных шагах
5. Разные комбинации параметров

---

## Шаг 8: CMake (`tests/CMakeLists.txt`)

Добавить тест-таргет в конец файла:

```cmake
add_executable(my_component_tests
    test_my_component.cpp
)
target_include_directories(my_component_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_SOURCE_DIR}/build/_deps/json-src/include
)
target_link_libraries(my_component_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(my_component_tests)
```

---

## Шаг 9: Собрать и запустить

```bash
cd /Users/vladimir/an24_cpp
cmake --build build -j$(sysctl -n hw.ncpu)
cd build && ctest --output-on-failure -R my_component
```

---

## Чеклист

- [ ] `components/<Name>.json` создан с правильными портами и параметрами
- [ ] Класс объявлен в `all.h` с `Provider provider` и `static constexpr Domain domain`
- [ ] Реализация в `all.cpp` с `template <typename Provider>`
- [ ] Строка `template class Name<JitProvider>;` добавлена в `explicit_instantiations.h`
- [ ] Codegen запущен, `port_registry.h` обновлён (не вручную!)
- [ ] Фабрика в `jit_solver.cpp` содержит `else if (dev.classname == "Name")`
- [ ] Тесты написаны и проходят
- [ ] Полная сборка проекта без ошибок

---

## Частые ошибки

1. **Забыли запустить codegen** → `PortNames::xxx` или `ComponentType::xxx` не компилируется
2. **Забыли explicit instantiation** → linker error: undefined reference to `MyComponent<JitProvider>::...`
3. **Забыли фабрику** → runtime throw: "Unknown component type: MyComponent"
4. **Редактировали port_registry.h вручную** → изменения пропадут при следующем codegen
5. **Имя порта в JSON не совпадает с `PortNames::xxx`** → codegen создаст enum, но в коде используется другое имя
6. **Нет `stamp_` в solve_electrical для выходного порта** → SOR не "видит" узел, значение не обновляется
