#include "render.h"
#include <cmath>

namespace {

// Цвета
constexpr uint32_t COLOR_NODE_BG = 0xFF404040;    // темно-серый
constexpr uint32_t COLOR_NODE_BORDER = 0xFF808080; // светло-серый
constexpr uint32_t COLOR_TEXT = 0xFFFFFFFF;       // белый
constexpr uint32_t COLOR_WIRE = 0xFF00FFFF;      // cyan
constexpr uint32_t COLOR_GRID = 0x3C3C3C3C;      // полупрозрачный серый
constexpr uint32_t COLOR_SELECTED = 0xFF00FF00; // зеленый

// Найти позицию порта на узле
Pt get_port_position(const Node& node, const char* port_name) {
    // Для простоты - порты по бокам
    // Input - слева, Output - справа

    // Сначала ищем в inputs
    for (size_t i = 0; i < node.inputs.size(); i++) {
        if (node.inputs[i].name == port_name) {
            float y_offset = node.size.y * (float)(i + 1) / (float)(node.inputs.size() + 1);
            return Pt(node.pos.x, node.pos.y + y_offset);
        }
    }

    // Потом в outputs
    for (size_t i = 0; i < node.outputs.size(); i++) {
        if (node.outputs[i].name == port_name) {
            float y_offset = node.size.y * (float)(i + 1) / (float)(node.outputs.size() + 1);
            return Pt(node.pos.x + node.size.x, node.pos.y + y_offset);
        }
    }

    // По умолчанию - центр
    return Pt(node.pos.x + node.size.x / 2, node.pos.y + node.size.y / 2);
}

} // namespace

void render_blueprint(const Blueprint& bp, IDrawList* dl, const Viewport& vp, Pt canvas_min, Pt canvas_max) {
    // Рендерим провода (сначала, чтобы были под узлами)
    for (const auto& w : bp.wires) {
        // Находим узлы
        const Node* start_node = nullptr;
        const Node* end_node = nullptr;

        for (const auto& n : bp.nodes) {
            if (n.id == w.start.node_id) start_node = &n;
            if (n.id == w.end.node_id) end_node = &n;
        }

        if (!start_node || !end_node) continue;

        Pt start_pos = get_port_position(*start_node, w.start.port_name.c_str());
        Pt end_pos = get_port_position(*end_node, w.end.port_name.c_str());

        // Преобразуем в экранные координаты
        Pt screen_start = vp.world_to_screen(start_pos, canvas_min);
        Pt screen_end = vp.world_to_screen(end_pos, canvas_min);

        // Если есть routing points - используем polyline
        if (!w.routing_points.empty()) {
            // Собираем точки
            Pt points[64]; // max points
            size_t count = 0;

            points[count++] = screen_start;
            for (const auto& rp : w.routing_points) {
                if (count >= 63) break;
                points[count++] = vp.world_to_screen(rp, canvas_min);
            }
            points[count++] = screen_end;

            dl->add_polyline(points, count, COLOR_WIRE, 2.0f);
        } else {
            // Простая линия
            dl->add_line(screen_start, screen_end, COLOR_WIRE, 2.0f);
        }
    }

    // Рендерим узлы
    for (const auto& n : bp.nodes) {
        Pt screen_min = vp.world_to_screen(n.pos, canvas_min);
        Pt screen_max = vp.world_to_screen(Pt(n.pos.x + n.size.x, n.pos.y + n.size.y), canvas_min);

        // Rect узла
        dl->add_rect_filled(screen_min, screen_max, COLOR_NODE_BG);
        dl->add_rect(screen_min, screen_max, COLOR_NODE_BORDER, 1.0f);

        // Текст - имя узла
        Pt text_pos(screen_min.x + 5, screen_min.y + 5);
        dl->add_text(text_pos, n.name.c_str(), COLOR_TEXT, 12.0f);
    }
}

void render_grid(IDrawList* dl, const Viewport& vp, Pt canvas_min, Pt canvas_max) {
    float step = vp.grid_step;

    // Определяем границы в мировых координатах
    Pt tl = vp.screen_to_world(canvas_min, canvas_min);
    Pt br = vp.screen_to_world(canvas_max, canvas_min);

    int x0 = (int)std::floor(tl.x / step);
    int x1 = (int)std::ceil(br.x / step);
    int y0 = (int)std::floor(tl.y / step);
    int y1 = (int)std::ceil(br.y / step);

    // Рисуем точки сетки
    float dot_radius = 1.0f * vp.zoom;
    if (dot_radius < 0.5f) dot_radius = 0.5f;
    if (dot_radius > 3.0f) dot_radius = 3.0f;

    for (int gx = x0; gx <= x1; gx++) {
        for (int gy = y0; gy <= y1; gy++) {
            Pt wp((float)gx * step, (float)gy * step);
            Pt sp = vp.world_to_screen(wp, canvas_min);

            // Проверяем что точка в пределах canvas
            if (sp.x < canvas_min.x || sp.x > canvas_max.x ||
                sp.y < canvas_min.y || sp.y > canvas_max.y) {
                continue;
            }

            dl->add_circle_filled(sp, dot_radius, COLOR_GRID, 6);
        }
    }
}
