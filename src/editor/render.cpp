#include "render.h"
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <cstring>

namespace {

// Цвета (ImGui: byte0=R, byte1=G, byte2=B, byte3=A -> 0xAABBGGRR)
constexpr uint32_t COLOR_TEXT = 0xFFFFFFFF;       // белый
constexpr uint32_t COLOR_TEXT_DIM = 0xFFAAAAAA;   // серый текст
constexpr uint32_t COLOR_WIRE = 0xFF50DCFF;      // золотистый (255, 220, 80)
constexpr uint32_t COLOR_WIRE_SELECTED = 0xFF96C8FF;
constexpr uint32_t COLOR_GRID = 0xFF404040;      // темно-серый
constexpr uint32_t COLOR_SELECTED = 0xFF00FF00;  // зеленый
constexpr uint32_t COLOR_PORT_INPUT = 0xFFDCDCB4;   // синеватый
constexpr uint32_t COLOR_PORT_OUTPUT = 0xFFDCB4B4;  // красноватый

// Node styles - по типу
struct NodeColors {
    uint32_t fill;     // 0xAABBGGRR
    uint32_t border;
};

NodeColors get_node_colors(const char* type_name) {
    static const std::unordered_map<std::string, NodeColors> styles = {
        {"battery",   {0xFF788C3C, 0xFF285028}}, // зеленый
        {"relay",     {0xFF328C78, 0xFF281E1E}}, // красный
        {"lightbulb", {0xFFF0A032, 0xFF1E5014}}, // желтый
        {"pump",      {0xFF325A82, 0xFF1E3C5A}}, // синий
        {"valve",     {0xFF325A82, 0xFF5A3C1E}}, // оранжевый
        {"sensor",    {0xFF824646, 0xFF462878}}, // фиолетовый
        {"subsystem", {0xFF328282, 0xFF1E5A46}}, // бирюзовый
        {"motor",     {0xFF646432, 0xFF46461E}}, // желто-зеленый
        {"generator", {0xFF5A8232, 0xFF3C5050}}, // зеленый
        {"switch",    {0xFFF0Be32, 0xFF5A461E}}, // желтый
        {"bus",       {0xFF505050, 0xFF323250}}, // синий серый
        {"gyroscope", {0xFF6E4A82, 0xFF4A2E5A}}, // фиолетовый
        {"agk47",    {0xFFBE5032, 0xFF7A321E}}, // оранжевый
        {"refnode",  {0xFF323232, 0xFF1E1E1E}}, // темный
    };

    if (!type_name || type_name[0] == '\0') {
        return {0xFF505050, 0xFF323232}; // default серый
    }

    std::string key = type_name;
    std::transform(key.begin(), key.end(), key.begin(), ::tolower);

    auto it = styles.find(key);
    if (it != styles.end()) {
        return it->second;
    }
    return {0xFF505050, 0xFF323232}; // default серый
}

// Найти позицию порта на узле
Pt get_port_position(const Node& node, const char* port_name) {
    // Input - слева, Output - справа

    int num_inputs = (int)node.inputs.size();
    int num_outputs = (int)node.outputs.size();
    int max_ports = std::max(num_inputs, num_outputs);
    if (max_ports == 0) max_ports = 1;

    // Сначала ищем в inputs
    for (size_t i = 0; i < node.inputs.size(); i++) {
        if (node.inputs[i].name == port_name) {
            float t = (float)(i + 1) / (float)(max_ports + 1);
            return Pt(node.pos.x, node.pos.y + node.size.y * t);
        }
    }

    // Потом в outputs
    for (size_t i = 0; i < node.outputs.size(); i++) {
        if (node.outputs[i].name == port_name) {
            float t = (float)(i + 1) / (float)(max_ports + 1);
            return Pt(node.pos.x + node.size.x, node.pos.y + node.size.y * t);
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

        // Получаем цвета по типу узла
        NodeColors colors = get_node_colors(n.type_name.c_str());

        // Rect узла - фон
        dl->add_rect_filled(screen_min, screen_max, colors.fill);
        dl->add_rect(screen_min, screen_max, colors.border, 1.0f);

        // Header - более темная полоска сверху
        float header_height = 20.0f * vp.zoom;
        Pt header_min(screen_min.x, screen_min.y);
        Pt header_max(screen_max.x, screen_min.y + header_height);
        uint32_t header_color = (colors.fill & 0xFF000000) | // A
                                 ((colors.fill >> 1) & 0x007F7F7F); // темнее
        dl->add_rect_filled(header_min, header_max, header_color);

        // Текст - имя узла в header
        Pt text_pos(screen_min.x + 5, screen_min.y + header_height / 2 - 6);
        dl->add_text(text_pos, n.name.c_str(), COLOR_TEXT, 12.0f);

        // Тип узла под header
        if (!n.type_name.empty()) {
            Pt type_pos(screen_min.x + 5, screen_min.y + header_height + 5);
            dl->add_text(type_pos, n.type_name.c_str(), COLOR_TEXT_DIM, 10.0f);
        }

        // Порты
        float port_radius = 4.0f;

        int num_inputs = (int)n.inputs.size();
        int num_outputs = (int)n.outputs.size();
        int max_ports = std::max(num_inputs, num_outputs);
        if (max_ports == 0) max_ports = 1;

        // Входные порты (слева)
        for (int i = 0; i < num_inputs; i++) {
            float t = (float)(i + 1) / (float)(max_ports + 1);
            Pt port_pos(screen_min.x, screen_min.y + header_height + (n.size.y - header_height) * t * vp.zoom);
            dl->add_circle_filled(port_pos, port_radius, COLOR_PORT_INPUT, 8);
            // Подпись порта
            Pt label_pos(screen_min.x - 30, port_pos.y - 5);
            dl->add_text(label_pos, n.inputs[i].name.c_str(), COLOR_TEXT_DIM, 9.0f);
        }

        // Выходные порты (справа)
        for (int i = 0; i < num_outputs; i++) {
            float t = (float)(i + 1) / (float)(max_ports + 1);
            Pt port_pos(screen_max.x, screen_min.y + header_height + (n.size.y - header_height) * t * vp.zoom);
            dl->add_circle_filled(port_pos, port_radius, COLOR_PORT_OUTPUT, 8);
            // Подпись порта
            Pt label_pos(screen_max.x + 5, port_pos.y - 5);
            dl->add_text(label_pos, n.outputs[i].name.c_str(), COLOR_TEXT_DIM, 9.0f);
        }
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

    // Рисуем линии сетки
    float line_width = 0.5f;

    // Вертикальные линии
    for (int gx = x0; gx <= x1; gx++) {
        Pt wp_start((float)gx * step, tl.y);
        Pt wp_end((float)gx * step, br.y);
        Pt sp_start = vp.world_to_screen(wp_start, canvas_min);
        Pt sp_end = vp.world_to_screen(wp_end, canvas_min);
        dl->add_line(sp_start, sp_end, COLOR_GRID, line_width);
    }

    // Горизонтальные линии
    for (int gy = y0; gy <= y1; gy++) {
        Pt wp_start(tl.x, (float)gy * step);
        Pt wp_end(br.x, (float)gy * step);
        Pt sp_start = vp.world_to_screen(wp_start, canvas_min);
        Pt sp_end = vp.world_to_screen(wp_end, canvas_min);
        dl->add_line(sp_start, sp_end, COLOR_GRID, line_width);
    }
}
