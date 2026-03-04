#include "render.h"
#include "trigonometry.h"
#include "router/crossings.h"
#include <algorithm>
#include <unordered_map>
#include <cstring>
#include <cmath>

namespace {

// Цвета (ImGui: byte0=R, byte1=G, byte2=B, byte3=A -> 0xAABBGGRR)
constexpr uint32_t COLOR_TEXT = 0xFFFFFFFF;       // белый
constexpr uint32_t COLOR_TEXT_DIM = 0xFFAAAAAA;   // серый текст
constexpr uint32_t COLOR_WIRE = 0xFF50DCFF;      // золотистый (255, 220, 80) - выделенный
constexpr uint32_t COLOR_WIRE_UNSEL = 0xFF606060; // серый - невыделенный
constexpr uint32_t COLOR_GRID = 0xFF404040;      // темно-серый
constexpr uint32_t COLOR_SELECTED = 0xFF00FF00;  // зеленый
constexpr uint32_t COLOR_PORT_INPUT = 0xFFDCDCB4;   // синеватый
constexpr uint32_t COLOR_PORT_OUTPUT = 0xFFDCB4B4;  // красноватый
constexpr uint32_t COLOR_ROUTING_POINT = 0xFFFF8000; // оранжевый
constexpr uint32_t COLOR_JUMP_ARC = 0xFF404040;    // темный для jump arc

// Radius of the semicircular arc drawn at wire crossings (world units)
constexpr float ARC_RADIUS_WORLD = 5.0f;
// Number of line segments used to approximate the semicircle
constexpr int ARC_SEGMENTS = 8;

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

} // namespace

void render_blueprint(const Blueprint& bp, IDrawList* dl, const Viewport& vp, Pt canvas_min, Pt canvas_max,
                      const std::vector<size_t>* selected_nodes, std::optional<size_t> selected_wire) {

    // Build polylines for all wires first (for crossing detection)
    std::vector<std::vector<Pt>> all_polylines;
    all_polylines.reserve(bp.wires.size());

    for (size_t wire_idx = 0; wire_idx < bp.wires.size(); wire_idx++) {
        const auto& w = bp.wires[wire_idx];

        const Node* start_node = nullptr;
        const Node* end_node = nullptr;
        for (const auto& n : bp.nodes) {
            if (n.id == w.start.node_id) start_node = &n;
            if (n.id == w.end.node_id) end_node = &n;
        }

        if (!start_node || !end_node) {
            all_polylines.push_back({});
            continue;
        }

        Pt start_pos = editor_math::get_port_position(*start_node, w.start.port_name.c_str());
        Pt end_pos = editor_math::get_port_position(*end_node, w.end.port_name.c_str());

        std::vector<Pt> poly;
        poly.push_back(start_pos);
        poly.insert(poly.end(), w.routing_points.begin(), w.routing_points.end());
        poly.push_back(end_pos);

        all_polylines.push_back(std::move(poly));
    }

    // Find all crossings (only higher-index wire draws arc)
    std::vector<std::vector<WireCrossing>> all_crossings;
    all_crossings.resize(bp.wires.size());

    for (size_t wire_idx = 0; wire_idx < bp.wires.size(); wire_idx++) {
        all_crossings[wire_idx] = find_wire_crossings(wire_idx, all_polylines);
    }

    // Рендерим провода (сначала, чтобы были под узлами)
    for (size_t wire_idx = 0; wire_idx < bp.wires.size(); wire_idx++) {
        const auto& w = bp.wires[wire_idx];

        // Цвет провода: выделенный - золотой, невыделенный - серый
        bool is_selected = selected_wire.has_value() && *selected_wire == wire_idx;
        uint32_t wire_color = is_selected ? COLOR_WIRE : COLOR_WIRE_UNSEL;
        // Находим узлы
        const Node* start_node = nullptr;
        const Node* end_node = nullptr;

        for (const auto& n : bp.nodes) {
            if (n.id == w.start.node_id) start_node = &n;
            if (n.id == w.end.node_id) end_node = &n;
        }

        if (!start_node || !end_node) continue;

        Pt start_pos = editor_math::get_port_position(*start_node, w.start.port_name.c_str());
        Pt end_pos = editor_math::get_port_position(*end_node, w.end.port_name.c_str());

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

            dl->add_polyline(points, count, wire_color, 2.0f);
        } else {
            // Простая линия
            dl->add_line(screen_start, screen_end, wire_color, 2.0f);
        }

        // Рендерим routing points как кружочки
        for (const auto& rp : w.routing_points) {
            Pt screen_rp = vp.world_to_screen(rp, canvas_min);
            dl->add_circle_filled(screen_rp, 6.0f, COLOR_ROUTING_POINT, 12);
            dl->add_circle(screen_rp, 6.0f, 0xFF000000, 12);
        }

        // Рендерим jump arcs (только для этого провода - он имеет больший индекс)
        const auto& crossings = all_crossings[wire_idx];
        for (const auto& crossing : crossings) {
            Pt world_cross = crossing.pos;
            Pt screen_cross = vp.world_to_screen(world_cross, canvas_min);

            // Determine arc direction based on which segment is horizontal/vertical
            // For simplicity, draw a semi-circle
            float arc_radius = ARC_RADIUS_WORLD * vp.zoom;

            // Draw arc as polyline (semicircle)
            Pt arc_points[ARC_SEGMENTS + 1];
            for (int i = 0; i <= ARC_SEGMENTS; i++) {
                float angle = 3.14159265f * i / ARC_SEGMENTS; // 0 to PI
                // Arc goes "up" (positive Y in screen space)
                Pt offset(std::cos(angle) * arc_radius, std::sin(angle) * arc_radius);
                arc_points[i] = Pt(screen_cross.x + offset.x, screen_cross.y - offset.y);
            }
            dl->add_polyline(arc_points, ARC_SEGMENTS + 1, COLOR_JUMP_ARC, 2.0f);
        }
    }

    // Рендерим узлы
    size_t node_idx = 0;
    for (const auto& n : bp.nodes) {
        // Bus - маленький квадрат, все порты посередине
        if (n.kind == NodeKind::Bus) {
            Pt screen_center = vp.world_to_screen(
                Pt(n.pos.x + n.size.x / 2, n.pos.y + n.size.y / 2), canvas_min);
            float size = 30.0f * vp.zoom;
            Pt bus_min(screen_center.x - size, screen_center.y - size);
            Pt bus_max(screen_center.x + size, screen_center.y + size);

            NodeColors colors = get_node_colors(n.type_name.c_str());
            dl->add_rect_filled(bus_min, bus_max, colors.fill);
            dl->add_rect(bus_min, bus_max, colors.border, 1.0f);
            dl->add_text(Pt(bus_min.x + 3, screen_center.y - 5), n.name.c_str(), COLOR_TEXT, 10.0f);

            // Порты - все в центре
            float port_radius = 6.0f;
            for (size_t i = 0; i < n.inputs.size(); i++) {
                dl->add_circle_filled(screen_center, port_radius, COLOR_PORT_INPUT, 8);
            }
            for (size_t i = 0; i < n.outputs.size(); i++) {
                dl->add_circle_filled(screen_center, port_radius, COLOR_PORT_OUTPUT, 8);
            }
        }
        // Ref - маленький квадрат, порт сверху
        else if (n.kind == NodeKind::Ref) {
            Pt screen_center = vp.world_to_screen(
                Pt(n.pos.x + n.size.x / 2, n.pos.y + n.size.y / 2), canvas_min);
            float size = 20.0f * vp.zoom;
            Pt ref_min(screen_center.x - size, screen_center.y - size);
            Pt ref_max(screen_center.x + size, screen_center.y + size);

            NodeColors colors = get_node_colors(n.type_name.c_str());
            dl->add_rect_filled(ref_min, ref_max, colors.fill);
            dl->add_rect(ref_min, ref_max, colors.border, 1.0f);
            dl->add_text(Pt(ref_min.x + 2, screen_center.y - 5), n.name.c_str(), COLOR_TEXT, 10.0f);

            // Порт сверху
            Pt port_pos(screen_center.x, ref_min.y);
            float port_radius = 5.0f;
            if (!n.outputs.empty()) {
                dl->add_circle_filled(port_pos, port_radius, COLOR_PORT_OUTPUT, 8);
            } else if (!n.inputs.empty()) {
                dl->add_circle_filled(port_pos, port_radius, COLOR_PORT_INPUT, 8);
            }
        }
        // Обычный Node - прямоугольник с header и портами по сторонам
        else {
            Pt screen_min = vp.world_to_screen(n.pos, canvas_min);
            Pt screen_max = vp.world_to_screen(Pt(n.pos.x + n.size.x, n.pos.y + n.size.y), canvas_min);

            NodeColors colors = get_node_colors(n.type_name.c_str());
            dl->add_rect_filled(screen_min, screen_max, colors.fill);

            // Выделение
            bool is_selected = false;
            if (selected_nodes) {
                for (size_t idx : *selected_nodes) {
                    if (idx == node_idx) { is_selected = true; break; }
                }
            }
            if (is_selected) {
                dl->add_rect(screen_min, screen_max, COLOR_SELECTED, 2.0f);
            } else {
                dl->add_rect(screen_min, screen_max, colors.border, 1.0f);
            }

            // Header
            float header_height = 20.0f * vp.zoom;
            Pt header_min(screen_min.x, screen_min.y);
            Pt header_max(screen_max.x, screen_min.y + header_height);
            uint32_t header_color = (colors.fill & 0xFF000000) | ((colors.fill >> 1) & 0x007F7F7F);
            dl->add_rect_filled(header_min, header_max, header_color);

            // Текст
            dl->add_text(Pt(screen_min.x + 5, screen_min.y + header_height / 2 - 6), n.name.c_str(), COLOR_TEXT, 12.0f);
            if (!n.type_name.empty()) {
                dl->add_text(Pt(screen_min.x + 5, screen_min.y + header_height + 5), n.type_name.c_str(), COLOR_TEXT_DIM, 10.0f);
            }

            // Порты
            float port_radius = 4.0f;
            for (size_t i = 0; i < n.inputs.size(); i++) {
                Pt port_world = editor_math::get_port_position(n, n.inputs[i].name.c_str());
                Pt port_pos = vp.world_to_screen(port_world, canvas_min);
                dl->add_circle_filled(port_pos, port_radius, COLOR_PORT_INPUT, 8);
                dl->add_text(Pt(screen_min.x - 30, port_pos.y - 5), n.inputs[i].name.c_str(), COLOR_TEXT_DIM, 9.0f);
            }
            for (size_t i = 0; i < n.outputs.size(); i++) {
                Pt port_world = editor_math::get_port_position(n, n.outputs[i].name.c_str());
                Pt port_pos = vp.world_to_screen(port_world, canvas_min);
                dl->add_circle_filled(port_pos, port_radius, COLOR_PORT_OUTPUT, 8);
                dl->add_text(Pt(screen_max.x + 5, port_pos.y - 5), n.outputs[i].name.c_str(), COLOR_TEXT_DIM, 9.0f);
            }
        }

        node_idx++;
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
