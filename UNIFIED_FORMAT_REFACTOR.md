# Unified JSON Format: Отказ от раздельных Blueprint/Component

## Цель

Объединить два раздельных JSON формата (`components/*.json` и `blueprints/*.json`) в единый формат.
Компонент — это Blueprint без внутренностей. Blueprint — это компонент с вложенной схемой.

**Обратная совместимость не требуется. Все форматы и структуры можно менять свободно.**

---

## 1. Текущая архитектура (AS-IS)

### 1.1 Два JSON формата

**Component JSON** (`components/Battery.json`) — определение типа:

```json
{
  "classname": "Battery",
  "description": "Battery voltage source with internal resistance and capacity",
  "default_ports": {
    "v_in": { "direction": "In", "type": "V" },
    "v_out": { "direction": "Out", "type": "V" }
  },
  "default_params": { "v_nominal": "28.0", "internal_r": "0.01" },
  "default_domains": ["Electrical"],
  "default_priority": "high",
  "default_critical": true,
  "default_content_type": "None",
  "default_size": [6, 4]
}
```

**Blueprint JSON** (`blueprints/simple_battery.json`) — граф экземпляров:

```json
{
  "description": "Simple battery module",
  "devices": [
    { "name": "gnd", "classname": "RefNode", "params": { "value": "0.0" } },
    {
      "name": "bat",
      "classname": "Battery",
      "params": { "v_nominal": "28.0" }
    },
    {
      "name": "vin",
      "classname": "BlueprintInput",
      "params": { "exposed_type": "V", "exposed_direction": "In" }
    },
    {
      "name": "vout",
      "classname": "BlueprintOutput",
      "params": { "exposed_type": "V", "exposed_direction": "Out" }
    }
  ],
  "connections": [
    { "from": "vin.port", "to": "bat.v_in" },
    { "from": "bat.v_out", "to": "vout.port" },
    { "from": "gnd.v", "to": "vin.port" }
  ]
}
```

**Editor JSON** (расширенный формат для визуального редактора):

```json
{
  "devices": [
    {
      "name": "bat1",
      "classname": "Battery",
      "kind": "Node",
      "pos": { "x": 100, "y": 200 },
      "size": { "x": 120, "y": 80 },
      "ports": { "v_in": { "direction": "In", "type": "V" } },
      "params": { "v_nominal": "28.0" },
      "group_id": ""
    },
    {
      "name": "lamp",
      "classname": "lamp_pass_through",
      "kind": "Blueprint",
      "blueprint_path": "blueprints/lamp_pass_through.json",
      "pos": { "x": 300, "y": 200 }
    }
  ],
  "wires": [{ "from": "bat1.v_out", "to": "lamp.v_in", "routing_points": [] }],
  "collapsed_groups": [
    { "id": "lamp", "blueprint_path": "...", "type_name": "lamp_pass_through" }
  ],
  "viewport": { "pan": { "x": 0, "y": 0 }, "zoom": 1.0, "grid_step": 20.0 }
}
```

### 1.2 Два пути загрузки

| Путь               | Функция                      | Файл                                  | Что делает                                                                                      |
| ------------------ | ---------------------------- | ------------------------------------- | ----------------------------------------------------------------------------------------------- |
| Component registry | `load_component_registry()`  | `src/json_parser/json_parser.cpp`     | Сканирует `components/*.json`, заполняет `ComponentRegistry`                                    |
| Simulation parse   | `parse_json()`               | `src/json_parser/json_parser.cpp`     | Парсит blueprint JSON, рекурсивно раскрывает nested blueprints через `merge_nested_blueprint()` |
| Editor load        | `load_editor_format()`       | `src/editor/visual/scene/persist.cpp` | Парсит editor JSON в `Blueprint` struct с `Node[]` + `Wire[]`                                   |
| Editor save (sim)  | `blueprint_to_json()`        | `src/editor/visual/scene/persist.cpp` | `Blueprint` → simulation JSON (пропускает Blueprint-kind nodes, перезаписывает wire endpoints)  |
| Editor save (edit) | `blueprint_to_editor_json()` | `src/editor/visual/scene/persist.cpp` | `Blueprint` → editor JSON (все nodes + wires + viewport + collapsed_groups)                     |

### 1.3 Ключевые структуры данных

| Структура             | Файл                                        | Роль                                                           |
| --------------------- | ------------------------------------------- | -------------------------------------------------------------- |
| `ComponentDefinition` | `src/json_parser/json_parser.h`             | Определение типа компонента (из component JSON)                |
| `DeviceInstance`      | `src/json_parser/json_parser.h`             | Экземпляр устройства для simulation (из blueprint JSON)        |
| `ParserContext`       | `src/json_parser/json_parser.h`             | Результат парсинга: `devices[]` + `connections[]` + `registry` |
| `ComponentRegistry`   | `src/json_parser/json_parser.h`             | Кэш `ComponentDefinition` по classname                         |
| `Node`                | `src/editor/data/node.h`                    | Узел в визуальном редакторе                                    |
| `Blueprint`           | `src/editor/data/blueprint.h`               | Схема в визуальном редакторе: `nodes[]` + `wires[]`            |
| `ComponentVariant`    | `src/jit_solver/components/port_registry.h` | `std::variant<Battery<P>, Switch<P>, ...>` для JIT             |

### 1.4 `NodeKind` enum

```cpp
enum class NodeKind {
    Node,       // Обычный компонент (Battery, Pump, etc.)
    Bus,        // Шина/мультиплексор
    Ref,        // RefNode (ground)
    Blueprint   // Свернутый nested blueprint (double-clickable)
};
```

### 1.5 Проблемы текущей архитектуры

1. **Дублирование** — порты описаны в `components/X.json` И в каждом editor-файле, где X используется
2. **Два парсера** — `load_component_registry()` и `parse_json()` делают похожую работу по-разному
3. **Неконсистентность** — component vs blueprint имеют разные поля (`default_ports` vs `ports`, `default_params` vs `params`)
4. **Два каталога** — `components/` и `blueprints/` создают искусственное разделение
5. **Rigid NodeKind** — `NodeKind::Node` по-разному рисуется от `NodeKind::Blueprint`, хотя концептуально оба — "чёрный ящик с входами/выходами"

---

## 2. Целевая архитектура (TO-BE)

### 2.1 Единый JSON формат: `library/*.json`

Каждый файл в `library/` описывает **один тип** — будь то C++ компонент или составная схема:

**C++ компонент** (`library/Battery.json`):

```json
{
  "classname": "Battery",
  "description": "Battery voltage source with internal resistance and capacity",
  "cpp_class": true,
  "ports": {
    "v_in": { "direction": "In", "type": "V" },
    "v_out": { "direction": "Out", "type": "V" }
  },
  "params": {
    "v_nominal": "28.0",
    "internal_r": "0.01",
    "capacity": "1000.0",
    "charge": "1000.0"
  },
  "domains": ["Electrical"],
  "priority": "high",
  "critical": true,
  "content_type": "None",
  "size": [6, 4]
}
```

**Blueprint** (`library/simple_battery.json`):

```json
{
  "classname": "SimpleBattery",
  "description": "Simple battery module with exposed input/output ports",
  "cpp_class": false,
  "ports": {
    "v_in": { "direction": "In", "type": "V" },
    "v_out": { "direction": "Out", "type": "V" }
  },
  "params": {},
  "domains": ["Electrical"],
  "priority": "high",
  "critical": true,
  "devices": [
    { "name": "gnd", "classname": "RefNode", "params": { "value": "0.0" } },
    {
      "name": "bat",
      "classname": "Battery",
      "params": { "v_nominal": "28.0" }
    },
    {
      "name": "vin",
      "classname": "BlueprintInput",
      "params": { "exposed_type": "V", "exposed_direction": "In" }
    },
    {
      "name": "vout",
      "classname": "BlueprintOutput",
      "params": { "exposed_type": "V", "exposed_direction": "Out" }
    }
  ],
  "connections": [
    { "from": "vin.port", "to": "bat.v_in" },
    { "from": "bat.v_out", "to": "vout.port" },
    { "from": "gnd.v", "to": "vin.port" }
  ]
}
```

**Разница** — ТОЛЬКО поле `cpp_class` и наличие/отсутствие `devices`+`connections`:

| Поле          | C++ компонент | Blueprint                   |
| ------------- | ------------- | --------------------------- |
| `cpp_class`   | `true`        | `false`                     |
| `ports`       | ✅            | ✅ (exposed ports)          |
| `params`      | ✅ (defaults) | ✅ (defaults для blueprint) |
| `domains`     | ✅            | ✅                          |
| `devices`     | отсутствует   | ✅ (внутренние компоненты)  |
| `connections` | отсутствует   | ✅ (внутренние соединения)  |

### 2.2 Единый `TypeDefinition` (замена `ComponentDefinition`)

```cpp
struct TypeDefinition {
    std::string classname;
    std::string description;
    bool cpp_class = false;                          // true = C++ impl, false = blueprint

    // Общие поля
    std::unordered_map<std::string, Port> ports;     // exposed ports
    std::unordered_map<std::string, std::string> params;  // default params
    std::vector<Domain> domains;
    std::string priority = "med";
    bool critical = false;
    std::string content_type = "None";
    std::optional<std::pair<float, float>> size;     // grid units {w, h}

    // Только для blueprint (cpp_class == false)
    std::vector<DeviceInstance> devices;              // internal devices
    std::vector<Connection> connections;              // internal connections

    bool is_blueprint() const { return !cpp_class; }
    bool is_leaf() const { return cpp_class; }
};
```

### 2.3 Единый `TypeRegistry` (замена `ComponentRegistry`)

```cpp
struct TypeRegistry {
    std::unordered_map<std::string, TypeDefinition> types;

    const TypeDefinition* get(const std::string& classname) const;
    bool has(const std::string& classname) const;
    std::vector<std::string> list_classnames() const;
    std::vector<std::string> list_cpp_classes() const;
    std::vector<std::string> list_blueprints() const;
};
```

### 2.4 Новый `NodeKind`

```cpp
enum class NodeKind {
    Node,           // Обычный компонент (Battery, Pump, etc.) — visual backward compat
    Bus,            // Шина/мультиплексор
    Ref,            // RefNode (ground)
    Blueprint,      // Свернутый nested blueprint (double-clickable, expandable)
    InternalCPP     // Leaf C++ component (визуально как Blueprint, но не expandable)
};
```

**Поведение в editor:**

| NodeKind      | Отображение                    | Double-click         | Drill-down |
| ------------- | ------------------------------ | -------------------- | ---------- |
| `Node`        | Стандартный box с content      | Нет                  | Нет        |
| `Bus`         | Маленький квадрат              | Нет                  | Нет        |
| `Ref`         | Маленький треугольник          | Нет                  | Нет        |
| `Blueprint`   | Box как Node, показывает ports | Открывает sub-window | Да         |
| `InternalCPP` | Box как Node, показывает ports | Нет действия         | Нет        |

> **Вопрос**: нужен ли вообще `NodeKind::Node` если все C++ компоненты переходят на `InternalCPP`?
> **Ответ**: На первом этапе — оставляем оба. `Node` для совместимости с текущим рендерингом (gauges, switches). Позже `Node` может стать deprecated в пользу `InternalCPP` + content.

### 2.5 Путь данных (data flow)

```
library/*.json
      │
      ▼
  TypeRegistry           ← единый парсер: load_type_registry("library/")
      │
      ├──► Editor: TypeDefinition → Node (с NodeKind::InternalCPP или NodeKind::Blueprint)
      │           ports, params, content из TypeDefinition
      │
      ├──► JIT:   TypeDefinition → DeviceInstance[] (flatten blueprints рекурсивно)
      │           → ComponentVariant → solve()
      │
      └──► AOT:   TypeDefinition → DeviceInstance[] (flatten) → CodeGen → C++ source
```

---

## 3. Что меняется, что НЕ меняется

### 3.1 НЕ МЕНЯЕТСЯ (zero impact)

- **C++ классы компонентов** (`Battery<P>`, `Switch<P>`, etc.) — не трогаем
- **`ComponentVariant` (std::variant)** — тот же набор типов
- **`create_component_variant()`** — работает по `classname`, как и раньше
- **`setup_component_ports()`** — маппинг port→signal
- **SOR solver** — работает с `SimulationState`, не знает об JSON
- **Domain scheduling** — multi-domain bucket system
- **Provider pattern** — `JitProvider`, `AotProvider`
- **Port macros** — `PORTS(...)` определения в C++ компонентах
- **`CodeGen`** — принимает `DeviceInstance[]` + `Connection[]`, источник неважен
- **Визуальный рендеринг VisualNode** — остаётся тем же, добавляется обработка `InternalCPP`

### 3.2 МЕНЯЕТСЯ

| Область                       | Текущее                           | Целевое                                                         | Сложность        |
| ----------------------------- | --------------------------------- | --------------------------------------------------------------- | ---------------- |
| JSON формат                   | 2 формата                         | 1 формат                                                        | Low (mechanical) |
| Каталог                       | `components/` + `blueprints/`     | `library/`                                                      | Low (file move)  |
| `ComponentDefinition`         | `default_ports`, `default_params` | `TypeDefinition` с `ports`, `params` + `devices`, `connections` | Medium           |
| `ComponentRegistry`           | Только components                 | `TypeRegistry` — все типы                                       | Medium           |
| `load_component_registry()`   | Парсит только component format    | `load_type_registry()` — единый парсер                          | Medium           |
| `parse_json()`                | Раскрывает blueprints fallback    | Использует `TypeRegistry` для раскрытия                         | Medium           |
| `NodeKind`                    | 4 варианта                        | 5 вариантов (+InternalCPP)                                      | Low              |
| `VisualNodeFactory::create()` | 4 cases                           | 5 cases                                                         | Low              |
| `on_double_click()`           | Only Blueprint                    | Blueprint=expand, InternalCPP=no-op                             | Low              |
| `add_component()` в editor    | Читает ComponentRegistry          | Читает TypeRegistry                                             | Low              |
| `persist.cpp` save/load       | Два формата save                  | Один формат + kind mapping                                      | Medium           |
| **45 component JSON файлов**  | `default_ports`, `default_params` | `ports`, `params`, `cpp_class: true`                            | **Mechanical**   |
| **2 blueprint JSON файла**    | Без `ports`, без `classname`      | С `ports`, `classname`, `cpp_class: false`                      | Low              |
| **Тесты**                     | Используют старые структуры       | Обновить на новые                                               | Medium           |

---

## 4. TODO: Пошаговый план рефакторинга (TDD)

### Phase 0: Подготовка

- [ ] **0.1** Создать `library/` директорию
- [ ] **0.2** Убедиться что все текущие тесты проходят (green baseline)

### Phase 1: Data Structures (TDD — tests first)

- [ ] **1.1** Написать failing test: `TypeDefinition` парсится из unified JSON (cpp_class=true)
- [ ] **1.2** Написать failing test: `TypeDefinition` парсится из unified JSON (cpp_class=false, с devices/connections)
- [ ] **1.3** Написать failing test: `TypeRegistry` загружает все типы из `library/` директории
- [ ] **1.4** Написать failing test: `TypeRegistry::get()` возвращает и cpp компоненты, и blueprints
- [ ] **1.5** Реализовать `TypeDefinition` и `TypeRegistry` в `src/json_parser/json_parser.h`
- [ ] **1.6** Реализовать `load_type_registry()` в `src/json_parser/json_parser.cpp`
- [ ] **1.7** Green: все новые тесты проходят

### Phase 2: Unified JSON files (DELEGATED — mechanical work)

- [ ] **2.1** Конвертировать 45 component JSON → unified format в `library/`
- [ ] **2.2** Конвертировать 2 blueprint JSON → unified format в `library/`
- [ ] **2.3** Написать test: все файлы в `library/` валидно парсятся через `load_type_registry()`

### Phase 3: NodeKind::InternalCPP (TDD)

- [ ] **3.1** Написать failing test: `NodeKind::InternalCPP` serializes/deserializes в editor format
- [ ] **3.2** Добавить `InternalCPP` в `NodeKind` enum
- [ ] **3.3** Обновить `persist.cpp`: load/save handle InternalCPP
- [ ] **3.4** Обновить `VisualNodeFactory::create()`: InternalCPP → VisualNode (как Blueprint, без content)
- [ ] **3.5** Обновить `on_double_click()`: InternalCPP = no-op
- [ ] **3.6** Green: все тесты проходят

### Phase 4: Wire up TypeRegistry → Editor (TDD)

- [ ] **4.1** Написать failing test: `add_component()` читает из `TypeRegistry` вместо `ComponentRegistry`
- [ ] **4.2** Написать failing test: cpp_class=true components создают `NodeKind::InternalCPP`
- [ ] **4.3** Написать failing test: cpp_class=false blueprints создают `NodeKind::Blueprint`
- [ ] **4.4** Обновить `EditorApp` — заменить `ComponentRegistry` на `TypeRegistry`
- [ ] **4.5** Обновить `add_component()` — использует `TypeDefinition.ports` вместо `ComponentDefinition.default_ports`
- [ ] **4.6** Green: все тесты проходят

### Phase 5: Wire up TypeRegistry → Simulation (TDD)

- [ ] **5.1** Написать failing test: `parse_json()` использует `TypeRegistry` для раскрытия blueprints
- [ ] **5.2** Написать failing test: simulation с unified-format blueprint работает идентично старому формату
- [ ] **5.3** Обновить `parse_json()` — раскрытие blueprint через `TypeRegistry.get(classname)`
- [ ] **5.4** Green: все simulation тесты проходят

### Phase 6: Удаление старого кода

- [ ] **6.1** Удалить `ComponentDefinition` (заменена на `TypeDefinition`)
- [ ] **6.2** Удалить `ComponentRegistry` (заменена на `TypeRegistry`)
- [ ] **6.3** Удалить `load_component_registry()` (заменена на `load_type_registry()`)
- [ ] **6.4** Удалить `components/` и `blueprints/` директории
- [ ] **6.5** Обновить все оставшиеся тесты, использующие старые структуры
- [ ] **6.6** Обновить `CONTEXT.md` и другие документы
- [ ] **6.7** Full green: все тесты проходят, никаких warnings

### Phase 7: Cleanup

- [ ] **7.1** Rename `default_*` поля в тестах и helpers
- [ ] **7.2** Обновить AOT codegen если нужно (проверить что port_registry generation работает)
- [ ] **7.3** Smoke test: editor загружает `library/`, показывает все компоненты, blueprints expandable, InternalCPP не expandable

---

## 5. Sub-prompts для делегирования

### SUB-PROMPT 1: Конвертация 45 component JSON → unified format

```
TASK: Convert all 45 component JSON files from the old format to the unified format.

SOURCE DIR: components/
TARGET DIR: library/

TRANSFORMATION RULES:
For each file in components/*.json, create the corresponding file in library/*.json with these changes:

1. Add field: "cpp_class": true
2. Rename "default_ports" → "ports"
3. Rename "default_params" → "params"
4. Rename "default_domains" → "domains"
5. Rename "default_priority" → "priority"
6. Rename "default_critical" → "critical"
7. Rename "default_content_type" → "content_type"
8. Rename "default_size" → "size"
9. Keep "classname" and "description" as-is
10. Do NOT add "devices" or "connections" fields (they are absent for cpp components)

EXAMPLE:

BEFORE (components/Battery.json):
{
  "classname": "Battery",
  "description": "Battery voltage source with internal resistance and capacity",
  "default_ports": {
    "v_in": { "direction": "In", "type": "V" },
    "v_out": { "direction": "Out", "type": "V" }
  },
  "default_params": {
    "v_nominal": "28.0",
    "internal_r": "0.01",
    "capacity": "1000.0",
    "charge": "1000.0"
  },
  "default_domains": ["Electrical"],
  "default_priority": "high",
  "default_critical": true
}

AFTER (library/Battery.json):
{
  "classname": "Battery",
  "description": "Battery voltage source with internal resistance and capacity",
  "cpp_class": true,
  "ports": {
    "v_in": { "direction": "In", "type": "V" },
    "v_out": { "direction": "Out", "type": "V" }
  },
  "params": {
    "v_nominal": "28.0",
    "internal_r": "0.01",
    "capacity": "1000.0",
    "charge": "1000.0"
  },
  "domains": ["Electrical"],
  "priority": "high",
  "critical": true
}

NOTE: Some files may have "default_content_type" and "default_size" fields - rename those too.
NOTE: Some files may NOT have all fields (e.g., no default_domains) - that's OK, just convert what exists.

Convert ALL 45 files. List of files:
AGK47.json, AND.json, Add.json, Any_V_to_Bool.json, Battery.json, BlueprintInput.json,
BlueprintOutput.json, Bus.json, Comparator.json, DMR400.json, Divide.json, ElectricHeater.json,
ElectricPump.json, GS24.json, Generator.json, Gyroscope.json, HighPowerLoad.json,
HoldButton.json, IndicatorLight.json, InertiaNode.json, Inverter.json, LerpNode.json,
Load.json, Merger.json, Multiply.json, NAND.json, NOT.json, OR.json, P.json, PD.json,
PI.json, PID.json, Positive_V_to_Bool.json, RU19A.json, RUG82.json, Radiator.json,
RefNode.json, Relay.json, Resistor.json, SolenoidValve.json, Splitter.json, Subtract.json,
Switch.json, TempSensor.json, Transformer.json, Voltmeter.json, XOR.json
```

### SUB-PROMPT 2: Конвертация 2 blueprint JSON → unified format

```
TASK: Convert 2 blueprint JSON files from the old format to the unified format.

SOURCE DIR: blueprints/
TARGET DIR: library/

TRANSFORMATION RULES:
1. Add "classname" field derived from filename (e.g., "simple_battery.json" → "SimpleBattery")
2. Add "cpp_class": false
3. Add "ports" field — extract from BlueprintInput/BlueprintOutput devices inside the blueprint:
   - BlueprintInput with exposed_direction="In" → port with direction "In"
   - BlueprintOutput with exposed_direction="Out" → port with direction "Out"
   - Port type comes from "exposed_type" param
   - Port name comes from device "name" (e.g., "vin", "vout")
4. Keep "description", "devices", "connections" (rename from "connections" if needed)
5. Add "domains" inferred from the devices inside (e.g., if contains Battery → Electrical)
6. Add "priority": "high" and "critical": true as defaults

EXAMPLE:

BEFORE (blueprints/simple_battery.json):
{
  "description": "Simple battery module with exposed input/output ports",
  "devices": [
    {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}},
    {"name": "bat", "classname": "Battery", "params": {"v_nominal": "28.0", "internal_r": "0.01"}},
    {"name": "vin", "classname": "BlueprintInput", "params": {"exposed_type": "V", "exposed_direction": "In"}},
    {"name": "vout", "classname": "BlueprintOutput", "params": {"exposed_type": "V", "exposed_direction": "Out"}}
  ],
  "connections": [
    {"from": "vin.port", "to": "bat.v_in"},
    {"from": "bat.v_out", "to": "vout.port"},
    {"from": "gnd.v", "to": "vin.port"}
  ]
}

AFTER (library/SimpleBattery.json):
{
  "classname": "SimpleBattery",
  "description": "Simple battery module with exposed input/output ports",
  "cpp_class": false,
  "ports": {
    "vin":  { "direction": "In",  "type": "V" },
    "vout": { "direction": "Out", "type": "V" }
  },
  "params": {},
  "domains": ["Electrical"],
  "priority": "high",
  "critical": true,
  "devices": [
    {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}},
    {"name": "bat", "classname": "Battery", "params": {"v_nominal": "28.0", "internal_r": "0.01"}},
    {"name": "vin", "classname": "BlueprintInput", "params": {"exposed_type": "V", "exposed_direction": "In"}},
    {"name": "vout", "classname": "BlueprintOutput", "params": {"exposed_type": "V", "exposed_direction": "Out"}}
  ],
  "connections": [
    {"from": "vin.port", "to": "bat.v_in"},
    {"from": "bat.v_out", "to": "vout.port"},
    {"from": "gnd.v", "to": "vin.port"}
  ]
}

FILES TO CONVERT:
1. blueprints/simple_battery.json → library/SimpleBattery.json
2. blueprints/lamp_pass_through.json → library/LampPassThrough.json
```

### SUB-PROMPT 3: Обновить все тесты, использующие `ComponentDefinition` → `TypeDefinition`

```
TASK: Update all test files that reference ComponentDefinition, ComponentRegistry,
load_component_registry, default_ports, default_params to use the new names.

SEARCH-AND-REPLACE rules (in all test files under tests/):

1. ComponentDefinition → TypeDefinition
2. ComponentRegistry → TypeRegistry
3. load_component_registry → load_type_registry
4. .default_ports → .ports
5. .default_params → .params
6. .default_domains → .domains
7. .default_priority → .priority
8. .default_critical → .critical
9. .default_content_type → .content_type
10. .default_size → .size
11. "components/" (path string) → "library/"

IMPORTANT: Only update references to the struct FIELDS and FUNCTION NAMES.
Do NOT change C++ component class names (Battery, Switch, etc.)
Do NOT change port names (v_in, v_out, control, etc.)
Do NOT change test logic or assertions.

List of test files that likely reference these:
- tests/json_parser_test.cpp
- tests/test_blueprint_loading.cpp
- tests/test_blueprint_integration.cpp
- tests/test_blueprint_ports.cpp
- tests/test_logical_solver.cpp
- tests/factory_validation_test.cpp
- tests/editor_componentvariant_test.cpp
- tests/test_data.cpp
- tests/test_persist.cpp (renamed NodeKind handling)

Also update src/ files:
- src/editor/app.h — ComponentRegistry → TypeRegistry
- src/editor/app.cpp — load_component_registry → load_type_registry, all default_* → *
- src/json_parser/json_parser.h — struct rename
- src/json_parser/json_parser.cpp — function rename + parsing logic
```

---

## 6. Risk Assessment

| Risk                                                         | Impact             | Mitigation                                                          |
| ------------------------------------------------------------ | ------------------ | ------------------------------------------------------------------- |
| Blueprint expansion loop (A references B which references A) | Infinite recursion | Max depth limit in `parse_json()` (already exists)                  |
| Missing component classname in `create_component_variant()`  | Runtime crash      | `TypeRegistry` validates all classnames on load                     |
| Editor regression (visual glitch)                            | Visual             | Existing render tests + manual smoke test                           |
| AOT codegen breaks                                           | Build failure      | `port_registry_test.cpp` catches it                                 |
| `BlueprintInput`/`BlueprintOutput` dual nature               | Confusion          | They are `cpp_class: true` with special classname, no change needed |

---

## 7. Estimated Change Impact

| Category                  | Files         | Lines (approx)  |
| ------------------------- | ------------- | --------------- |
| New struct definitions    | 1             | ~30             |
| New parser function       | 1             | ~80             |
| NodeKind addition         | 3-4           | ~20             |
| Component JSON conversion | 45            | Mechanical      |
| Blueprint JSON conversion | 2             | Mechanical      |
| Test updates              | 10-15         | ~200            |
| Old code removal          | 2-3           | -150            |
| **Total net**             | **~20 files** | **~+180 lines** |
