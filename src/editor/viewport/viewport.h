#pragma once

#include "data/pt.h"

/// Viewport - состояние канвы (pan, zoom)
///
/// Отвечает за координатные преобразования между экранными и мировыми координатами.
struct Viewport {
    Pt pan;           ///< Мировые координаты начала экрана
    float zoom;       ///< Уровень zoom (1.0 = 100%)

    Viewport();

    /// Преобразование: экранные координаты → мировые координаты
    Pt screen_to_world(Pt screen, Pt canvas_min) const;

    /// Преобразование: мировые координаты → экранные координаты
    Pt world_to_screen(Pt world, Pt canvas_min) const;

    /// Сдвиг viewport (в экранных пикселях)
    void pan_by(Pt screen_delta);

    /// Zoom к точке на экране
    void zoom_at(float delta, Pt screen_pos, Pt canvas_min);

    /// Ограничить zoom в допустимый диапазон
    void clamp_zoom();
};
