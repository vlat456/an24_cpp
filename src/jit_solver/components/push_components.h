#pragma once

#include "component.h"
#include <cstdint>
#include <string>

namespace an24 {

// Forward declaration
class PushState;

/// Push-based Switch - propagates voltage when closed
class PushSwitch : public Component {
public:
    std::string name;
    uint32_t v_in_idx = 0;
    uint32_t v_out_idx = 0;
    uint32_t control_idx = 0;
    uint32_t state_idx = 0;

    bool closed = false;
    float last_control = 0.0f;
    float r_closed = 0.01f;  // 10mΩ when closed

    PushSwitch() = default;
    PushSwitch(uint32_t v_in, uint32_t v_out, uint32_t control, uint32_t state, bool is_closed = false)
        : v_in_idx(v_in), v_out_idx(v_out), control_idx(control), state_idx(state), closed(is_closed) {}

    [[nodiscard]] std::string_view type_name() const override { return "Switch"; }
    [[nodiscard]] ComponentFlags flags() const override { return ComponentFlags::StateMachine; }

    /// Push voltage: if closed, V_out ≈ V_in; if open, V_out = 0
    void push_voltage(PushState& state, float dt) {
        float v_in = state.get_voltage("v_in");

        if (closed) {
            // Closed: propagate with small voltage drop
            // I = V_in / R_load (computing downstream current)
            // V_out = V_in - I * R_wire
            state.set_voltage("v_out", v_in);  // TODO: add voltage drop
        } else {
            // Open: no voltage pass-through
            state.set_voltage("v_out", 0.0f);
        }
    }

    /// Update state from control signal (edge detection)
    void update_state(PushState& state) {
        float v_control = state.get_voltage("control");

        // Toggle on any change (0 → 1 or 1 → 0)
        if (std::abs(v_control - last_control) > 0.1f) {
            closed = !closed;
        }
        last_control = v_control;

        // Output state: 1V = closed, 0V = open
        state.set_voltage("state", closed ? 1.0f : 0.0f);
    }
};

/// Push-based HoldButton - press-and-hold button
class PushHoldButton : public Component {
public:
    std::string name;
    uint32_t v_in_idx = 0;
    uint32_t v_out_idx = 0;
    uint32_t control_idx = 0;
    uint32_t state_idx = 0;

    bool is_pressed = false;
    float last_control = 0.0f;
    float r_closed = 0.01f;  // 10mΩ when pressed

    PushHoldButton() = default;
    PushHoldButton(uint32_t v_in, uint32_t v_out, uint32_t control, uint32_t state)
        : v_in_idx(v_in), v_out_idx(v_out), control_idx(control), state_idx(state) {}

    [[nodiscard]] std::string_view type_name() const override { return "HoldButton"; }
    [[nodiscard]] ComponentFlags flags() const override { return ComponentFlags::StateMachine; }

    /// Push voltage: if pressed, V_out ≈ V_in
    void push_voltage(PushState& state, float dt) {
        float v_in = state.get_voltage("v_in");

        if (is_pressed) {
            state.set_voltage("v_out", v_in);
        } else {
            state.set_voltage("v_out", 0.0f);
        }
    }

    /// Update state from control signal (1.0V = press, 2.0V = release)
    void update_state(PushState& state) {
        float v_control = state.get_voltage("control");

        // Edge detection: 0→1 = press, 1→2 = release
        if (std::abs(v_control - 1.0f) < 0.1f && std::abs(last_control - 1.0f) >= 0.1f) {
            is_pressed = true;
        } else if (std::abs(v_control - 2.0f) < 0.1f && std::abs(last_control - 2.0f) >= 0.1f) {
            is_pressed = false;
        }
        last_control = v_control;

        // Output state: 1V = pressed, 0V = released
        state.set_voltage("state", is_pressed ? 1.0f : 0.0f);
    }
};

/// Push-based Battery - voltage source with internal resistance
class PushBattery : public Component {
public:
    std::string name;
    uint32_t v_in_idx = 0;   // Ground reference
    uint32_t v_out_idx = 0;  // Output

    float v_nominal = 28.0f;    // Nominal voltage
    float internal_r = 0.01f;   // Internal resistance (10mΩ)
    float charge = 1000.0f;     // Current charge (Ah)

    PushBattery() = default;
    PushBattery(uint32_t v_in, uint32_t v_out, float v_nom, float r_int, bool load = false)
        : v_in_idx(v_in), v_out_idx(v_out), v_nominal(v_nom), internal_r(r_int) {
        (void)load;  // No longer needed, we use downstream resistance
    }

    [[nodiscard]] std::string_view type_name() const override { return "Battery"; }
    [[nodiscard]] ComponentFlags flags() const override { return ComponentFlags::VoltageSource; }

    /// Push voltage: V_out = V_nominal - I * R_internal
    /// Current I depends on downstream load resistance
    /// Charging mode: if V_in > V_nominal, battery charges
    void push_voltage(PushState& state, float dt) {
        float v_in = state.signals[v_in_idx].voltage;

        // Check if charging (V_in > V_nominal) or discharging
        bool is_charging = (v_in > v_nominal);

        float v_out;
        if (is_charging) {
            // Charging mode: V_out ≈ V_in (diode drop)
            // Current flows INTO battery: I = (V_in - V_nominal) / R_internal
            float i_charge = (v_in - v_nominal) / internal_r;
            // V_out = V_in - I * R_internal = V_in - (V_in - V_nominal) = V_nominal
            // But with charging, output should be slightly above nominal
            v_out = v_nominal + (i_charge * internal_r * 0.1f);  // Small charging boost
        } else {
            // Discharging mode
            // Get downstream load resistance
            float r_load = state.signals[v_out_idx].resistance;

            // If resistance is very low (wire/short circuit), treat as no load
            // Wires shouldn't cause voltage sag - they're just conductors
            if (r_load < 1.0f) {
                r_load = 1e9f;  // 1 GΩ = essentially open circuit
            }

            // I = V_nominal / (R_internal + R_load)
            float i_load = v_nominal / (internal_r + r_load);

            // Voltage sag due to internal resistance
            v_out = v_nominal - (i_load * internal_r);
        }

        // Set output voltage using index
        state.signals[v_out_idx].voltage = v_out;
    }
};

/// Push-based IndicatorLight - simple resistive load
class PushIndicatorLight : public Component {
public:
    std::string name;
    uint32_t v_in_idx = 0;
    uint32_t v_out_idx = 0;
    uint32_t brightness_idx = 0;

    float r_on = 100.0f;  // 100Ω when lit
    float v_threshold = 1.0f;  // Turn on above 1V

    PushIndicatorLight() = default;
    PushIndicatorLight(uint32_t v_in, uint32_t v_out, uint32_t brightness)
        : v_in_idx(v_in), v_out_idx(v_out), brightness_idx(brightness) {
        // TODO: remove these when we parse params properly
        (void)v_in; (void)v_out; (void)brightness;
    }

    [[nodiscard]] std::string_view type_name() const override { return "IndicatorLight"; }
    [[nodiscard]] ComponentFlags flags() const override { return ComponentFlags::PropagatesResistance; }

    /// Set resistance at input (load resistance)
    void propagate_resistance(PushState& state) {
        state.signals[v_in_idx].resistance = r_on;
    }

    /// Push voltage: V_out = V_in - I * R
    /// Brightness = V_in / V_threshold (clamped 0-1)
    void push_voltage(PushState& state, float dt) {
        float v_in = state.signals[v_in_idx].voltage;

        // I = V_in / R
        float i = v_in / r_on;

        // V_out = V_in - I * R = V_in - V_in = 0V (all voltage dropped across lamp)
        state.signals[v_out_idx].voltage = 0.0f;

        // Brightness based on input voltage
        float brightness = std::clamp(v_in / v_threshold, 0.0f, 1.0f);
        state.signals[brightness_idx].voltage = brightness;
    }
};

/// Push-based Resistor - simple resistive load
class PushResistor : public Component {
public:
    std::string name;
    uint32_t v_in_idx = 0;
    uint32_t v_out_idx = 0;

    float resistance = 100.0f;  // Ohms

    PushResistor() = default;
    PushResistor(uint32_t v_in, uint32_t v_out, float r)
        : v_in_idx(v_in), v_out_idx(v_out), resistance(r) {
        (void)v_in; (void)v_out;
    }

    [[nodiscard]] std::string_view type_name() const override { return "Resistor"; }
    [[nodiscard]] ComponentFlags flags() const override { return ComponentFlags::PropagatesResistance; }

    /// Propagate resistance upstream (for series circuit computation)
    void propagate_resistance(PushState& state) {
        // Get downstream resistance (what's connected to v_out)
        float r_downstream = state.signals[v_out_idx].resistance;

        // Total resistance seen at v_in = this resistor + downstream
        state.signals[v_in_idx].resistance = resistance + r_downstream;
    }

    /// Push voltage: V_out = V_in - I * R where I = V_in / R_total
    void push_voltage(PushState& state, float dt) {
        float v_in = state.signals[v_in_idx].voltage;

        // Total resistance (this + downstream)
        float r_total = resistance + state.signals[v_out_idx].resistance;

        if (r_total > 0.0f) {
            // I = V_in / R_total
            float i = v_in / r_total;

            // V_out = V_in - I * R = V_in - (V_in / R_total) * R
            state.signals[v_out_idx].voltage = v_in - (i * resistance);
        } else {
            // No downstream load - no voltage drop (open circuit)
            state.signals[v_out_idx].voltage = v_in;
        }
    }
};

/// Push-based Generator - voltage source that needs external excitation
class PushGenerator : public Component {
public:
    std::string name;
    uint32_t v_out_idx = 0;
    uint32_t rpm_idx = 0;

    float v_nominal = 28.0f;     // Nominal output voltage
    float rpm_threshold = 1000.0f;  // RPM needed to produce voltage
    float internal_r = 0.01f;    // Internal resistance

    PushGenerator() = default;
    PushGenerator(uint32_t v_out, uint32_t rpm)
        : v_out_idx(v_out), rpm_idx(rpm) {}

    [[nodiscard]] std::string_view type_name() const override { return "Generator"; }
    [[nodiscard]] ComponentFlags flags() const override { return ComponentFlags::VoltageSource; }

    /// Push voltage: produces voltage only when RPM > threshold and load connected
    void push_voltage(PushState& state, float dt) {
        float rpm = state.signals[rpm_idx].voltage;
        float r_load = state.signals[v_out_idx].resistance;

        // Need RPM above threshold AND load connected to produce voltage
        if (rpm < rpm_threshold || r_load < 0.001f) {
            state.signals[v_out_idx].voltage = 0.0f;
            return;
        }

        // V_out = V_nominal - I * R_internal
        float i_load = v_nominal / (internal_r + r_load);
        float v_out = v_nominal - (i_load * internal_r);

        state.signals[v_out_idx].voltage = v_out;
    }
};

/// Push-based RefNode - reference voltage source (ground, power supply, etc.)
class PushRefNode : public Component {
public:
    std::string name;
    uint32_t v_idx = 0;
    float value = 0.0f;

    PushRefNode() = default;
    PushRefNode(uint32_t v, float val)
        : v_idx(v), value(val) {}

    [[nodiscard]] std::string_view type_name() const override { return "RefNode"; }
    ComponentFlags flags() const override { return ComponentFlags::VoltageSource; }

    /// Push voltage: outputs fixed reference voltage
    void push_voltage(PushState& state, float dt) {
        state.signals[v_idx].voltage = value;
    }
};

/// Push-based Wire - near-zero resistance conductor
class PushWire : public Component {
public:
    std::string name;
    uint32_t v_in_idx = 0;
    uint32_t v_out_idx = 0;

    float r_wire = 0.001f;  // 1mΩ (very low resistance)

    PushWire() = default;
    PushWire(uint32_t v_in, uint32_t v_out)
        : v_in_idx(v_in), v_out_idx(v_out) {}

    [[nodiscard]] std::string_view type_name() const override { return "Wire"; }
    [[nodiscard]] ComponentFlags flags() const override { return ComponentFlags::PropagatesResistance; }

    /// Propagate resistance upstream
    void propagate_resistance(PushState& state) {
        float r_downstream = state.signals[v_out_idx].resistance;
        state.signals[v_in_idx].resistance = r_wire + r_downstream;
    }

    /// Push voltage: V_out ≈ V_in (negligible drop)
    void push_voltage(PushState& state, float dt) {
        float v_in = state.signals[v_in_idx].voltage;
        // Pass through with tiny voltage drop
        state.signals[v_out_idx].voltage = v_in * 0.9999f;  // 0.01% drop
    }
};

/// Push-based LerpNode - linear interpolation (first-order filter)
class PushLerpNode : public Component {
public:
    std::string name;
    uint32_t input_idx = 0;
    uint32_t output_idx = 0;

    float factor = 0.1f;  // Interpolation factor (0.0 to 1.0)
    float last_output = 0.0f;  // Previous output value (for smoothing)

    PushLerpNode() = default;
    PushLerpNode(uint32_t input, uint32_t output, float f = 0.1f)
        : input_idx(input), output_idx(output), factor(f) {}

    [[nodiscard]] std::string_view type_name() const override { return "LerpNode"; }
    [[nodiscard]] ComponentFlags flags() const override { return ComponentFlags::None; }

    /// Linear interpolation: output = output + (input - output) * factor
    /// This creates a first-order low-pass filter for sensor smoothing
    void push_voltage(PushState& state, float dt) {
        float input = state.signals[input_idx].voltage;

        // output += (input - output) * factor
        float delta = (input - last_output) * factor;
        last_output += delta;

        state.signals[output_idx].voltage = last_output;
    }
};

/// Стартер-генератор ГС-24 (двойной режим)
/// Режим СТАРТЕР: потребляет ток от аккумулятора, раскручивает ВСУ
/// Режим ГЕНЕРАТОР: выдает ток в бортсеть после 45% RPM
class PushGS24 : public Component {
public:
    std::string name;
    uint32_t v_start_idx = 0;   // Питание стартера (напрямую от батареи)
    uint32_t v_bus_idx = 0;     // Выход генератора (через ДМР на шину)
    uint32_t k_mod_idx = 0;     // Модуляция возбуждения (от РУГ-82)
    uint32_t rpm_out_idx = 0;   // Выход оборотов

    enum class Mode { OFF, STARTER, STARTER_WAIT, GENERATOR };
    Mode mode = Mode::OFF;

    // Переменные состояния
    float current_rpm = 0.0f;
    float target_rpm = 16000.0f;
    float wait_time = 0.0f;

    // Параметры стартера
    constexpr static float K_MOTOR_BACK_EMF = 38.0f;  // Противо-ЭДС при 100% RPM (вольт)
    constexpr static float R_INTERNAL = 0.025f;       // Внутреннее сопротивление 25мОм
    constexpr static float I_MIN_STARTER = 50.0f;     // Минимум 50А (удержание)
    constexpr static float I_MAX_STARTER = 800.0f;    // Максимум 800А (пуск)
    constexpr static float RPM_CUTOFF = 0.45f;        // Переключение на генератор при 45%

    // Параметры генератора
    constexpr static float I_MAX_GEN = 400.0f;        // Максимум 400А
    constexpr static float R_NORTON = 0.08f;          // Сопротивление Нортона
    constexpr static float RPM_THRESHOLD = 0.40f;     // 40% минимум для генерации

    PushGS24() = default;
    PushGS24(uint32_t v_start, uint32_t v_bus, uint32_t k_mod, uint32_t rpm_out)
        : v_start_idx(v_start), v_bus_idx(v_bus), k_mod_idx(k_mod), rpm_out_idx(rpm_out) {}

    [[nodiscard]] std::string_view type_name() const override { return "GS24"; }
    [[nodiscard]] ComponentFlags flags() const override {
        return ComponentFlags::StateMachine | ComponentFlags::VoltageSource;
    }

    /// Управление напряжением: двойной режим работы
    void push_voltage(PushState& state, float dt) {
        if (mode == Mode::STARTER) {
            // === РЕЖИМ СТАРТЕРА ===
            // Потребляет питание от v_start (напрямую от батареи)
            float v_start = state.signals[v_start_idx].voltage;

            // Противо-ЭДС: уменьшает эффективное напряжение с ростом RPM
            float rpm_percent = current_rpm / target_rpm;
            float back_emf = K_MOTOR_BACK_EMF * rpm_percent;  // ~38V при 100% RPM

            // V_effective = V_start - back_emf
            float v_effective = v_start - back_emf;

            // I = V / R (потребляемый ток)
            float i_consumed = v_effective / R_INTERNAL;

            // Ограничение тока
            if (i_consumed < I_MIN_STARTER) i_consumed = I_MIN_STARTER;
            if (i_consumed > I_MAX_STARTER) i_consumed = I_MAX_STARTER;

            // Напряжение на шине (с учетом падения на внутреннем сопротивлении)
            float v_drop = i_consumed * R_INTERNAL;
            state.signals[v_bus_idx].voltage = v_start - v_drop;

        } else if (mode == Mode::GENERATOR) {
            // === РЕЖИМ ГЕНЕРАТОРА ===
            // Выдает питание в шину (через ДМР)
            float rpm_percent = current_rpm / target_rpm;

            // Phi(RPM): 0 если <40%, линейно 40-60%, 1 если >60%
            float phi = 0.0f;
            if (rpm_percent >= 0.6f) {
                phi = 1.0f;
            } else if (rpm_percent >= RPM_THRESHOLD) {
                phi = (rpm_percent - RPM_THRESHOLD) / 0.2f;
            }

            // Получаем k_mod от РУГ-82 (по умолчанию 1.0 если не подключен)
            float k_mod = (k_mod_idx > 0) ? state.signals[k_mod_idx].voltage : 1.0f;

            // Выходное напряжение генератора (регулируется РУГ-82 до ~28.5V)
            float v_gen = 28.5f * phi * k_mod;

            // Ограничение реалистичного диапазона
            v_gen = std::clamp(v_gen, 0.0f, 30.0f);

            state.signals[v_bus_idx].voltage = v_gen;

        } else {
            // OFF или STARTER_WAIT - нет выхода
            state.signals[v_bus_idx].voltage = 0.0f;
        }

        // Вывод RPM для приборов
        state.signals[rpm_out_idx].voltage = current_rpm;
    }

    /// Обновление state machine (вызывается каждый кадр)
    void update_state(PushState& state, float dt) {
        (void)state;  // Не используется в push модели
        float rpm_percent = current_rpm / target_rpm;

        switch (mode) {
        case Mode::STARTER:
            // Разгон при прокрутке
            if (current_rpm < target_rpm * RPM_CUTOFF) {
                float acceleration = 300.0f;  // RPM в секунду
                current_rpm += acceleration * dt;
            }

            // Проверка перехода на генератор (45% RPM)
            if (rpm_percent >= RPM_CUTOFF) {
                current_rpm = target_rpm * RPM_CUTOFF;
                mode = Mode::STARTER_WAIT;
                wait_time = 0.0f;
            }
            break;

        case Mode::STARTER_WAIT:
            // Короткая пауза перед переключением на генератор
            wait_time += dt;
            if (wait_time >= 1.0f) {  // 1 секунда паузы
                mode = Mode::GENERATOR;
            }
            break;

        case Mode::GENERATOR: {
            // Продолжаем разгон до оборотов холостого хода
            float idle_rpm = target_rpm * 0.6f;  // 60% = холостой ход
            if (current_rpm < idle_rpm) {
                float acceleration = 500.0f;
                current_rpm += acceleration * dt;
                if (current_rpm > idle_rpm) current_rpm = idle_rpm;
            }
            break;
        }

        case Mode::OFF:
        default:
            current_rpm = 0.0f;
            break;
        }
    }
};

/// Регулятор напряжения РУГ-82
/// Угольный регулятор - поддерживает 28.5V на шине
/// Изменяет k_mod (коэффициент возбуждения) в зависимости от ошибки напряжения
class PushRUG82 : public Component {
public:
    std::string name;
    uint32_t v_gen_idx = 0;   // Напряжение генератора (мониторинг)
    uint32_t k_mod_idx = 0;   // Выход модуляции возбуждения

    float v_target = 28.5f;   // Целевое напряжение
    float k_mod = 1.0f;       // Текущая модуляция (0.0 ... 1.0)
    float kp = 0.5f;          // Пропорциональный коэффициент

    PushRUG82() = default;
    PushRUG82(uint32_t v_gen, uint32_t k_mod)
        : v_gen_idx(v_gen), k_mod_idx(k_mod) {}

    [[nodiscard]] std::string_view type_name() const override { return "RUG82"; }
    [[nodiscard]] ComponentFlags flags() const override { return ComponentFlags::None; }

    /// Управление напряжением: выдает k_mod на основе ошибки напряжения
    void push_voltage(PushState& state, float dt) {
        // Читаем напряжение генератора (напряжение шины)
        float v_gen = state.signals[v_gen_idx].voltage;

        // Вычисляем ошибку от целевого (28.5V)
        float error = v_target - v_gen;

        // ПИ-регулятор: если V слишком высоко - уменьшаем k_mod,
        // если V слишком низко - увеличиваем k_mod
        k_mod += kp * error * 0.01f;  // Малый коэффициент времени

        // Ограничиваем 0...1
        k_mod = std::clamp(k_mod, 0.0f, 1.0f);

        // Записываем k_mod на выход
        state.signals[k_mod_idx].voltage = k_mod;
    }
};

/// Дифференциально-минимальное реле ДМР-400
/// Подключает генератор к шине когда V_gen > V_bus
/// Управляет сигнальной лампой (горит когда отключен)
class PushDMR400 : public Component {
public:
    std::string name;
    uint32_t v_gen_in_idx = 0;   // Напряжение генератора (вход)
    uint32_t v_out_idx = 0;      // Выход на шину
    uint32_t v_bus_mon_idx = 0;  // Мониторинг шины (для сравнения и питания лампы)
    uint32_t lamp_idx = 0;       // Сигнальная лампа (выход)

    bool is_closed = false;                  // Контактор замкнут?
    float connect_threshold = 0.5f;          // Порог включения: V_gen > V_bus + 0.5V
    float disconnect_threshold = 10.0f;      // Порог отключения: V_bus > V_gen + 10V
    float reconnect_delay = 1.0f;            // Задержка перед повторным включением (сек)
    float reconnect_timer = 0.0f;            // Таймер задержки

    PushDMR400() = default;
    PushDMR400(uint32_t v_gen_in, uint32_t v_out, uint32_t v_bus_mon, uint32_t lamp)
        : v_gen_in_idx(v_gen_in), v_out_idx(v_out), v_bus_mon_idx(v_bus_mon), lamp_idx(lamp) {}

    [[nodiscard]] std::string_view type_name() const override { return "DMR400"; }
    [[nodiscard]] ComponentFlags flags() const override { return ComponentFlags::None; }

    /// Управление напряжением: подключение генератора к шине + лампа
    void push_voltage(PushState& state, float dt) {
        float v_gen = state.signals[v_gen_in_idx].voltage;
        float v_bus = state.signals[v_bus_mon_idx].voltage;

        if (!is_closed) {
            // Пытаемся включить: V_gen > V_bus + порог
            if (reconnect_timer <= 0.0f && v_gen > v_bus + connect_threshold) {
                is_closed = true;
            }

            // Контактор разомкнут: v_out не управляется (float)
            // НЕ устанавливаем напряжение в 0 - пусть остаётся как есть
            (void)v_out_idx;  // Не используется когда разомкнут

            // Лампа: подключаем к v_bus_mon (горит)
            if (lamp_idx > 0) {
                state.signals[lamp_idx].voltage = v_bus;
            }

        } else {
            // Проверка на обратный ток: V_bus значительно больше V_gen
            if (v_bus > v_gen + disconnect_threshold) {
                is_closed = false;
                reconnect_timer = reconnect_delay;
            }

            // Контактор замкнут: V_out = V_gen (проводник)
            state.signals[v_out_idx].voltage = v_gen;

            // Лампа: шунтируем на массу (не горит)
            if (lamp_idx > 0) {
                state.signals[lamp_idx].voltage = 0.0f;
            }
        }

        // Обновление таймера задержки
        if (reconnect_timer > 0.0f) {
            reconnect_timer -= dt;
        }
    }
};

/// ВСУ РУ19А-300 (Вспомогательная силовая установка)
/// State machine: OFF → CRANKING → IGNITION → RUNNING
/// Имеет тепловую модель T4 (температура газов за турбиной)
class PushRU19A : public Component {
public:
    std::string name;
    uint32_t v_start_idx = 0;   // Питание стартера (от батареи через кнопку)
    uint32_t v_out_idx = 0;     // Выход генератора
    uint32_t k_mod_idx = 0;     // Модуляция возбуждения (от РУГ-82)
    uint32_t rpm_out_idx = 0;   // Выход оборотов
    uint32_t t4_out_idx = 0;    // Выход температуры

    enum class APUState { OFF, CRANKING, IGNITION, RUNNING, STOPPING };
    APUState state = APUState::OFF;

    // Переменные состояния
    float current_rpm = 0.0f;
    float target_rpm = 16000.0f;
    float t4 = 20.0f;  // Температура (°C)
    float timer = 0.0f;
    uint32_t thermal_counter = 0;

    // Параметры
    constexpr static float AMBIENT_TEMP = 20.0f;     // Температура окружающая
    constexpr static float T4_TARGET = 400.0f;       // Целевая T4 (холостой ход)
    constexpr static float T4_MAX = 750.0f;          // Максимальная T4 (авария)
    constexpr static float CRANK_TIME = 2.0f;        // Время прокрутки (сек)
    constexpr static float IGNITION_TIME = 5.0f;     // Время зажигания (сек)
    constexpr static float START_TIMEOUT = 30.0f;    // Таймаут запуска (сек)
    constexpr static float IDLE_RPM_PERCENT = 0.6f;  // Холостой ход 60%

    // Тепловые параметры
    constexpr static float HEATING_RATE = 120.0f;           // Нагрев (°C/сек)
    constexpr static float BASE_COOLING = 110.0f;           // Базовое охлаждение
    constexpr static float THERMAL_STRESS_FACTOR = 50.0f;   // Тепловой стресс

    PushRU19A() = default;
    PushRU19A(uint32_t v_start, uint32_t v_out, uint32_t k_mod, uint32_t rpm_out, uint32_t t4_out)
        : v_start_idx(v_start), v_out_idx(v_out), k_mod_idx(k_mod),
          rpm_out_idx(rpm_out), t4_out_idx(t4_out) {}

    [[nodiscard]] std::string_view type_name() const override { return "RU19A"; }
    [[nodiscard]] ComponentFlags flags() const override {
        return ComponentFlags::StateMachine | ComponentFlags::VoltageSource;
    }

    /// Управление напряжением: двойной режим (стартер/потребитель vs генератор/источник)
    void push_voltage(PushState& state, float dt) {
        (void)dt;  // Не используется в этом методе
        float v_start = state.signals[v_start_idx].voltage;

        if (this->state == APUState::OFF || this->state == APUState::STOPPING) {
            // Нет выхода
            state.signals[v_out_idx].voltage = 0.0f;
            current_rpm = 0.0f;
            return;
        }

        if (this->state == APUState::CRANKING || this->state == APUState::IGNITION) {
            // === РЕЖИМ СТАРТЕРА ===
            // Потребляет питание от v_start (батарея)
            float rpm_percent = current_rpm / target_rpm;
            float back_emf = 38.0f * rpm_percent;  // 38V при 100% RPM

            // V_effective = V_start - back_emf
            float v_effective = v_start - back_emf;

            // Потребляем ток (резистивная нагрузка)
            float v_drop = v_effective * 0.1f;  // Падение на внутреннем сопротивлении
            state.signals[v_out_idx].voltage = v_start - v_drop;

        } else if (this->state == APUState::RUNNING) {
            // === РЕЖИМ ГЕНЕРАТОРА ===
            float rpm_percent = current_rpm / target_rpm;

            // Phi(RPM): 0 если <40%, линейно 40-60%, 1 если >60%
            float phi = 0.0f;
            if (rpm_percent >= 0.6f) {
                phi = 1.0f;
            } else if (rpm_percent >= 0.4f) {
                phi = (rpm_percent - 0.4f) / 0.2f;
            }

            // Получаем k_mod от РУГ-82
            float k_mod = (k_mod_idx > 0) ? state.signals[k_mod_idx].voltage : 1.0f;

            // V_out = 28.5V * phi * k_mod
            float v_out = 28.5f * phi * k_mod;
            v_out = std::clamp(v_out, 0.0f, 30.0f);

            state.signals[v_out_idx].voltage = v_out;
        }

        // Вывод RPM и T4
        state.signals[rpm_out_idx].voltage = current_rpm;
        state.signals[t4_out_idx].voltage = t4;
    }

    /// Обновление state machine (вызывается каждый кадр)
    void update_state(PushState& state, float dt) {
        float rpm_percent = current_rpm / target_rpm;
        timer += dt;

        // Тепловое обновление (1 Hz)
        thermal_counter++;
        if (thermal_counter >= 60) {
            thermal_counter = 0;
            update_thermal(dt);
        }

        switch (this->state) {
        case APUState::OFF:
            // Автозапуск если v_start > 10V
            if (state.signals[v_start_idx].voltage > 10.0f) {
                this->state = APUState::CRANKING;
                timer = 0.0f;
            }
            current_rpm = 0.0f;
            t4 = AMBIENT_TEMP;
            break;

        case APUState::CRANKING: {
            // Прокрутка 0-2 сек
            current_rpm += 500.0f * dt;
            if (current_rpm > 2000.0f) current_rpm = 2000.0f;

            if (timer >= CRANK_TIME) {
                this->state = APUState::IGNITION;
                timer = 0.0f;
            }
            break;
        }

        case APUState::IGNITION: {
            // Топливо включено, турбина помогает (2-5 сек)
            current_rpm += 1500.0f * dt;
            if (current_rpm > 5000.0f) current_rpm = 5000.0f;

            if (timer >= IGNITION_TIME) {
                this->state = APUState::RUNNING;
                timer = 0.0f;
            }

            // Проверка таймаута
            if (timer > START_TIMEOUT) {
                this->state = APUState::STOPPING;
            }
            break;
        }

        case APUState::RUNNING: {
            // Выход на холостые обороты (60%)
            float idle_rpm = target_rpm * IDLE_RPM_PERCENT;
            if (current_rpm < idle_rpm) {
                current_rpm += 2000.0f * dt;
                if (current_rpm > idle_rpm) current_rpm = idle_rpm;
            }
            break;
        }

        case APUState::STOPPING: {
            current_rpm -= 3000.0f * dt;
            if (current_rpm <= 0.0f) {
                current_rpm = 0.0f;
                this->state = APUState::OFF;
            }
            break;
        }
        }
    }

private:
    void update_thermal(float dt) {
        float dt_thermal = 1.0f;  // Тепловой работает на 1 Hz
        float rpm_percent = current_rpm / target_rpm;

        bool fuel_flowing = (this->state == APUState::IGNITION || this->state == APUState::RUNNING);

        if (fuel_flowing && this->state == APUState::IGNITION) {
            // Охлаждение зависит от RPM^2
            float rpm_factor = rpm_percent / 0.6f;
            float cooling = BASE_COOLING * rpm_factor * rpm_factor;

            // Тепловой стресс если RPM < 40% (слабая батарея)
            float thermal_stress = 0.0f;
            if (rpm_percent < 0.4f) {
                thermal_stress = (0.4f - rpm_percent) * 100.0f * THERMAL_STRESS_FACTOR;
            }

            float dT = (HEATING_RATE - cooling + thermal_stress) * dt_thermal;
            t4 += dT;
            if (t4 < AMBIENT_TEMP) t4 = AMBIENT_TEMP;

            // ПРОВЕРКА ГОРЯЧЕГО ЗАПУСКА
            if (t4 > T4_MAX) {
                this->state = APUState::STOPPING;  // Аварийная остановка
            }

        } else if (this->state == APUState::RUNNING) {
            // Стабилизация на целевой T4
            t4 += (T4_TARGET - t4) * dt_thermal * 0.5f;

        } else {
            // Охлаждение
            t4 -= (t4 - AMBIENT_TEMP) * dt_thermal * 2.0f;
            if (t4 < AMBIENT_TEMP) t4 = AMBIENT_TEMP;
        }
    }
};

} // namespace an24
