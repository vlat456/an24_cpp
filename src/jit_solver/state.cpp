#include "state.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace an24 {

uint32_t SimulationState::allocate_signal(float initial_value, SignalType type) {
    uint32_t idx = static_cast<uint32_t>(across.size());

    across.push_back(initial_value);
    through.push_back(0.0f);
    conductance.push_back(0.0f);
    inv_conductance.push_back(0.0f);
    signal_types.push_back(type);

    // Only dynamic signals count toward the iteration limit
    if (!type.is_fixed) {
        dynamic_signals_count = static_cast<uint32_t>(across.size());
    }

    return idx;
}

void SimulationState::resize_buffers(uint32_t signal_count) {
    convergence_buffer.resize(signal_count, 0.0f);
}

void SimulationState::clear_through() {
    // Use memset - faster than std::fill for small arrays
    std::memset(through.data(), 0, through.size() * sizeof(float));
    std::memset(conductance.data(), 0, conductance.size() * sizeof(float));
}

void SimulationState::precompute_inv_conductance() {
    // ================================================================
    // Безопасность SOR: добавляем паразитную проводимость
    // ================================================================
    // Проблема: если все реле разомкнуты, узел "висит" (floating node)
    // с проводимостью 0. При делении на 0 получаем inf/NaN → взрыв решателя.
    //
    // Решение: добавляем "утечку" на массу для каждого динамического узла.
    // Это очень маленькая проводимость (10 МОм), которая практически не
    // влияет на точность, но гарантирует что inv_conductance всегда > 0.
    //
    // Эффект: плавающие узлы будут стремиться к 0V (земля), что физично
    // и безопасно для численной стабильности.
    // ================================================================

    constexpr float PARASITIC_G = 1e-7f;  // 10 МОм утечка на массу (практически ничего)

    for (size_t i = 0; i < conductance.size(); ++i) {
        if (signal_types[i].is_fixed) {
            // Фиксированные сигналы (земля, источники питания) не обновляются SOR
            inv_conductance[i] = 0.0f;
        } else {
            // Добавляем паразитную проводимость к физической проводимости узла
            // Это предотвращает деление на ноль и дает "путь к земле"
            // для изолированных узлов
            float total_g = conductance[i] + PARASITIC_G;

            if (total_g > 1e-9f) {
                inv_conductance[i] = 1.0f / total_g;
            } else {
                // Даже с паразитной проводимостью этого не должно случиться
                // Но на всякий случай защищаемся от нуля
                inv_conductance[i] = 0.0f;
            }
        }
    }
}

void SimulationState::solve_signals_balance(float sor_omega) {
    // ====================================================================
    // SOR (Successive Over-Relaxation) - решатель для электрической сети
    // ====================================================================
    // Обновляет напряжения во всех узлах методом верхней релаксации:
    //   V_new = V_old + omega * (V_target - V_old)
    // где V_target = through / conductance (Нorton / Y-bus метод)
    //
    // МЕРА БЕЗОПАСНОСТИ:
    // При резком переключении реле (DMR400, Switch) или коротком замыкании
    // напряжение может "выстрелить" до бесконечности за одну итерацию.
    // Это приводит к NaN/Inf и полному краху решателя.
    //
    // Решение: ограничиваем изменение напряжения (delta clamping).
    // Максимально допустимый скачок: MAX_DELTA = 5V за итерацию.
    // ====================================================================

    constexpr float MAX_DELTA = 5.0f;  // Максимальное изменение за 1 итерацию SOR
    bool solver_exploded = false;      // Флаг для детекции краша решателя

    for (size_t i = 0; i < across.size(); ++i) {
        if (!signal_types[i].is_fixed && inv_conductance[i] > 0.0f) {
            // Вычисляем целевое напряжение (узловое напряжение Norton модели)
            // через = сумма токов от всех компонентов
            // conductance = сумма проводимостей всех компонентов
            // v_target = через / conductance
            float target_v = through[i] * inv_conductance[i];
            float current_v = across[i];

            // Delta - насколько хотим изменить напряжение
            // Сор振奋льный множитель omega ускоряет сходимость:
            // omega = 1.0 → обычный Гаусс-Зейдель (медленно)
            // omega = 1.5 → ускоренная сходимость (но менее стабильно)
            float delta = (target_v - current_v) * sor_omega;

            // ====================================================================
            // ЗАЩИТА ОТ "ВЗРЫВА" РЕШАТЕЛЯ (Delta Clamping)
            // ====================================================================
            // Проблема: при коротком замыкании или переключении реле
            // conductance может резко измениться → delta становится огромным
            //
            // Пример: реле DMR400 переключается → G меняется с 0 на 1000
            //          delta = (100 * 1000) * 1.5 = 150,000V! 💥
            //
            // Решение: ограничиваем delta разумными пределами.
            // Напряжение в бортсети самолета не может прыгнуть на 100V за
            // микросекунду - физически невозможно.
            // ====================================================================

            delta = std::clamp(delta, -MAX_DELTA, MAX_DELTA);

            // Применяем ограниченное изменение
            float new_v = current_v + delta;

            // ====================================================================
            // ДЕТЕКЦИЯ КРАША РЕШАТЕЛЯ (NaN/Inf Detection)
            // ====================================================================
            // Если что-то пошло не так (деление на ноль, переполнение),
            // new_v может стать NaN или Inf. Это заразит весь solver.
            //
            // Проверяем: std::isfinite() возвращает true только для
            // нормальных конечных чисел. NaN и Inf будут false.
            //
            // Признаки краша:
            // - std::isnan(v) = true (Not a Number)
            // - std::isinf(v) = true (Infinity)
            // ====================================================================

            if (!std::isfinite(new_v)) {
                // Решатель "взорвался"! Логируем и восстанавливаемся.
                spdlog::error("[SOR] Node {} exploded! v={:.2f} -> NaN/Inf. "
                               "Delta clamping failed. Resetting to 0V.",
                               i, current_v);
                across[i] = 0.0f;  // Безопасное значение по умолчанию
                solver_exploded = true;
            } else {
                // Всё в порядке, применяем новое напряжение
                across[i] = new_v;
            }
        }
    }

    // Логируем если решатель взорвался (для отладки)
    if (solver_exploded) {
        spdlog::warn("[SOR] Solver exploded this frame, but recovered. "
                      "Check for short circuits or sudden topology changes.");
    }
}

void SimulationState::solve_signals_balance_fast(float inv_omega) {
    // ====================================================================
    // Быстрая версия SOR с инвертированным omega
    // ====================================================================
    // inv_omega = 1.0 / sor_omega - умножение быстрее деления
    // Все защиты от взрыва решателя такие же, как в solve_signals_balance()
    //
    // Применяется когда sor_omega известен на этапе компиляции.
    // ====================================================================

    constexpr float MAX_DELTA = 5.0f;  // Максимальное изменение за 1 итерацию
    bool solver_exploded = false;

    for (size_t i = 0; i < across.size(); ++i) {
        if (!signal_types[i].is_fixed && inv_conductance[i] > 0.0f) {
            float current_v = across[i];
            float target_v = through[i] * inv_conductance[i];

            // Вычисляем delta с инвертированным omega (умножение вместо деления)
            // При omega = 1.5: inv_omega = 0.667
            // delta = (target_v - current_v) * inv_omega  - эквивалентно
            // delta = (target_v - current_v) / omega      но с умножением
            float delta = (target_v - current_v) * inv_omega;

            // Защита от взрыва (delta clamping)
            delta = std::clamp(delta, -MAX_DELTA, MAX_DELTA);

            float new_v = current_v + delta;

            // Детекция краша решателя (NaN/Inf)
            if (!std::isfinite(new_v)) {
                spdlog::error("[SOR Fast] Node {} exploded! v={:.2f} -> NaN/Inf. Resetting to 0V.",
                               i, current_v);
                across[i] = 0.0f;
                solver_exploded = true;
            } else {
                across[i] = new_v;
            }
        }
    }

    if (solver_exploded) {
        spdlog::warn("[SOR Fast] Solver exploded, recovered.");
    }
}

void SimulationState::save_convergence_state() {
    std::memcpy(convergence_buffer.data(), across.data(), across.size() * sizeof(float));
}

float SimulationState::get_max_change() const {
    float max_change = 0.0f;
    for (size_t i = 0; i < dynamic_signals_count; ++i) {
        float change = std::abs(across[i] - convergence_buffer[i]);
        if (change > max_change) {
            max_change = change;
        }
    }
    return max_change;
}

bool SimulationState::has_converged(float tolerance) const {
    for (uint32_t i = 0; i < dynamic_signals_count; ++i) {
        float delta = std::abs(across[i] - convergence_buffer[i]);
        if (delta > tolerance) {
            return false;
        }
    }
    return true;
}

} // namespace an24
