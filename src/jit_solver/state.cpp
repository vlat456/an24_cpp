#include "state.h"
#include <algorithm>
#include <cmath>
#include <cstring>

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
