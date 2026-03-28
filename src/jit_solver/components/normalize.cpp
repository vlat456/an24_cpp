#include "normalize.h"
#include "port_registry.h"
#include <algorithm>
#include <cmath>

template <typename Provider>
void Normalize<Provider>::pre_load() {
    // Предрасчитываем инверсный диапазон, чтобы избежать деления в solve
    float range = max - min;
    inv_range = (std::abs(range) > 1e-6f) ? (1.0f / range) : 0.0f;
}

template <typename Provider>
void Normalize<Provider>::solve_logical(SimulationState& st, float /*dt*/) {
    uint32_t in_idx = provider.get(PortNames::in);
    uint32_t out_idx = provider.get(PortNames::out);

    float input = st.values[in_idx];

    // Линейное преобразование: (x - min) * (1 / range)
    float normalized = (input - min) * inv_range;

    // Всегда ограничиваем результат в 0..1 для безопасности последующей логики
    st.values[out_idx] = std::clamp(normalized, 0.0f, 1.0f);
}

template <typename Provider>
void Normalize<Provider>::execute(SimulationState& st, float dt) {
    solve_logical(st, dt);
}

template class Normalize<JitProvider>;
