#include "render.h"
#include "trigonometry.h"
#include "router/crossings.h"
#include "visual_node.h"
#include "../jit_solver/simulator.h"
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

/// Get port color based on port type for visualization
/// Format: 0xAABBGGRR (A=alpha, B=blue, G=green, R=red)
uint32_t get_port_color(an24::PortType type) {
    switch (type) {
        case an24::PortType::V:
            return 0xFF0000FF;  // Red - Voltage
        case an24::PortType::I:
            return 0xFFFF0000;  // Blue - Current
        case an24::PortType::Bool:
            return 0xFF00FF00;  // Green - Boolean
        case an24::PortType::RPM:
            return 0xFF00A5FF;  // Orange - RPM
        case an24::PortType::Temperature:
            return 0xFF00FFFF;  // Yellow - Temperature
        case an24::PortType::Pressure:
            return 0xFFFFFF00;  // Cyan - Pressure
        case an24::PortType::Position:
            return 0xFF800080;  // Purple - Position
        case an24::PortType::Any:
            return 0xFF808080;  // Gray - Any/wildcard
        default:
            return 0xFF808080;  // Default gray
    }
}

void render_blueprint(const Blueprint& bp, IDrawList* dl, const Viewport& vp, Pt canvas_min, Pt canvas_max,
                      const std::vector<size_t>* selected_nodes, std::optional<size_t> selected_wire,
                      const an24::Simulator<an24::JIT_Solver>* simulation,
                      const Pt* hover_world_pos,
                      TooltipInfo* out_tooltip,
                      VisualNodeCache* cache) {

    // Wire has current color - yellow
    constexpr uint32_t COLOR_WIRE_CURRENT = 0xFF44AAFF; // yellow AA (FF,, 44)

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

        // Skip wires where either endpoint node is hidden (blueprint collapsing)
        if (!start_node->visible || !end_node->visible) {
            all_polylines.push_back({});
            continue;
        }

        Pt start_pos = editor_math::get_port_position(*start_node, w.start.port_name.c_str(), bp.wires, w.id.c_str(), cache);
        Pt end_pos = editor_math::get_port_position(*end_node, w.end.port_name.c_str(), bp.wires, w.id.c_str(), cache);

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
        const auto& poly = all_polylines[wire_idx];

        if (poly.size() < 2) continue;  // orphaned wire — skip

        // Цвет провода: выделенный - золотой, невыделенный - серый
        bool is_selected = selected_wire.has_value() && *selected_wire == wire_idx;
        uint32_t wire_color = is_selected ? COLOR_WIRE : COLOR_WIRE_UNSEL;

        // Подсветка проводов с током (симуляция запущена)
        if (simulation && simulation->is_running() && !w.start.node_id.empty()) {
            std::string start_port = w.start.node_id + "." + w.start.port_name;
            if (simulation->wire_is_energized(start_port, 0.5f)) {
                wire_color = COLOR_WIRE_CURRENT; // желтый - есть ток
            }
        }

        const auto& crossings = all_crossings[wire_idx];

        // Collect crossing positions sorted by distance along each segment
        // For each crossing, compute which segment it's on and the parametric t
        struct CrossOnSeg {
            size_t seg_idx;
            float t;
            Pt pos;
            SegDir my_seg_dir;
        };
        std::vector<CrossOnSeg> segs_crossings;
        for (const auto& c : crossings) {
            // Find which segment this crossing is on
            for (size_t i = 0; i + 1 < poly.size(); i++) {
                Pt a = poly[i], b = poly[i + 1];
                float seg_len_sq = (b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y);
                if (seg_len_sq < 1e-6f) continue;
                float t = ((c.pos.x - a.x) * (b.x - a.x) + (c.pos.y - a.y) * (b.y - a.y)) / seg_len_sq;
                if (t >= -0.01f && t <= 1.01f) {
                    // Check if point is actually on this segment
                    Pt proj(a.x + t * (b.x - a.x), a.y + t * (b.y - a.y));
                    float dist = std::sqrt((proj.x - c.pos.x) * (proj.x - c.pos.x) +
                                           (proj.y - c.pos.y) * (proj.y - c.pos.y));
                    if (dist < 1.0f) {
                        segs_crossings.push_back({i, std::max(0.0f, std::min(1.0f, t)), c.pos, c.my_seg_dir});
                        break;
                    }
                }
            }
        }

        // Draw the wire polyline with gaps at crossing points
        if (segs_crossings.empty()) {
            // No crossings — draw the whole polyline
            std::vector<Pt> screen_pts;
            screen_pts.reserve(poly.size());
            for (const auto& p : poly) {
                screen_pts.push_back(vp.world_to_screen(p, canvas_min));
            }
            dl->add_polyline(screen_pts.data(), screen_pts.size(), wire_color, 2.0f);
        } else {
            // Sort crossings by (segment index, t)
            std::sort(segs_crossings.begin(), segs_crossings.end(),
                      [](const CrossOnSeg& a, const CrossOnSeg& b) {
                          return a.seg_idx < b.seg_idx || (a.seg_idx == b.seg_idx && a.t < b.t);
                      });

            // Draw wire segments with gaps around each crossing
            // Gap radius in world coordinates
            float gap_r = ARC_RADIUS_WORLD;

            // We walk through the polyline and emit sub-polylines
            std::vector<Pt> current_sub;
            size_t cross_i = 0;

            for (size_t seg = 0; seg + 1 < poly.size(); seg++) {
                Pt a = poly[seg], b = poly[seg + 1];
                float seg_len = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));

                // Collect crossings on this segment
                std::vector<float> seg_ts;
                while (cross_i < segs_crossings.size() && segs_crossings[cross_i].seg_idx == seg) {
                    seg_ts.push_back(segs_crossings[cross_i].t);
                    cross_i++;
                }

                if (seg_ts.empty()) {
                    // No crossing on this segment — add both endpoints
                    if (current_sub.empty()) {
                        current_sub.push_back(vp.world_to_screen(a, canvas_min));
                    }
                    current_sub.push_back(vp.world_to_screen(b, canvas_min));
                } else {
                    // Has crossings — split the segment
                    float prev_t = 0.0f;
                    if (current_sub.empty()) {
                        current_sub.push_back(vp.world_to_screen(a, canvas_min));
                    }
                    for (float ct : seg_ts) {
                        float gap_t = (seg_len > 1e-3f) ? gap_r / seg_len : 0.5f;
                        float t_before = ct - gap_t;
                        float t_after  = ct + gap_t;

                        // Draw from prev_t to t_before
                        if (t_before > prev_t + 0.001f) {
                            Pt p_before(a.x + t_before * (b.x - a.x), a.y + t_before * (b.y - a.y));
                            current_sub.push_back(vp.world_to_screen(p_before, canvas_min));
                        }

                        // Flush current sub-polyline (end before gap)
                        if (current_sub.size() >= 2) {
                            dl->add_polyline(current_sub.data(), current_sub.size(), wire_color, 2.0f);
                        }
                        current_sub.clear();

                        // Start new sub-polyline after gap
                        if (t_after < 1.0f - 0.001f) {
                            Pt p_after(a.x + t_after * (b.x - a.x), a.y + t_after * (b.y - a.y));
                            current_sub.push_back(vp.world_to_screen(p_after, canvas_min));
                        }
                        prev_t = t_after;
                    }

                    // Continue after last crossing to segment end
                    if (prev_t < 1.0f - 0.001f) {
                        if (current_sub.empty()) {
                            // Crossing was near end, start fresh at the after-gap point
                            float gap_t = (seg_len > 1e-3f) ? gap_r / seg_len : 0.5f;
                            float last_after = seg_ts.back() + gap_t;
                            if (last_after < 1.0f) {
                                Pt p(a.x + last_after * (b.x - a.x), a.y + last_after * (b.y - a.y));
                                current_sub.push_back(vp.world_to_screen(p, canvas_min));
                            }
                        }
                        current_sub.push_back(vp.world_to_screen(b, canvas_min));
                    }
                }
            }

            // Flush last sub-polyline
            if (current_sub.size() >= 2) {
                dl->add_polyline(current_sub.data(), current_sub.size(), wire_color, 2.0f);
            }
        }

        // Рендерим routing points как кружочки
        for (const auto& rp : w.routing_points) {
            Pt screen_rp = vp.world_to_screen(rp, canvas_min);
            dl->add_circle_filled(screen_rp, 6.0f, COLOR_ROUTING_POINT, 12);
            dl->add_circle(screen_rp, 6.0f, 0xFF000000, 12);
        }

        // Рендерим jump arcs (только для этого провода - он имеет больший индекс)
        for (const auto& crossing : crossings) {
            Pt world_cross = crossing.pos;
            Pt screen_cross = vp.world_to_screen(world_cross, canvas_min);

            float arc_radius = ARC_RADIUS_WORLD * vp.zoom;

            // Arc direction is perpendicular to the wire segment:
            //   Horizontal wire → arc goes up (vertical)
            //   Vertical wire   → arc goes left/right (horizontal)
            bool arc_vertical = (crossing.my_seg_dir == SegDir::Horiz ||
                                 crossing.my_seg_dir == SegDir::Unknown);

            // Draw arc as polyline (semicircle)
            Pt arc_points[ARC_SEGMENTS + 1];
            for (int i = 0; i <= ARC_SEGMENTS; i++) {
                float angle = 3.14159265f * i / ARC_SEGMENTS; // 0 to PI
                if (arc_vertical) {
                    // Horizontal wire: arc goes upward
                    Pt offset(std::cos(angle) * arc_radius, std::sin(angle) * arc_radius);
                    arc_points[i] = Pt(screen_cross.x + offset.x, screen_cross.y - offset.y);
                } else {
                    // Vertical wire: arc goes to the right
                    Pt offset(std::sin(angle) * arc_radius, std::cos(angle) * arc_radius);
                    arc_points[i] = Pt(screen_cross.x + offset.x, screen_cross.y + offset.y);
                }
            }
            dl->add_polyline(arc_points, ARC_SEGMENTS + 1, wire_color, 2.0f);
        }
    }

    // Рендерим узлы - используем VisualNodeCache [h1a2b3c4]
    size_t node_idx = 0;
    for (const auto& n : bp.nodes) {
        // Use cache when available to avoid re-creating visuals every frame
        VisualNode* visual = nullptr;
        std::unique_ptr<VisualNode> visual_owned;
        if (cache) {
            visual = cache->getOrCreate(n, bp.wires);
        } else {
            visual_owned = VisualNodeFactory::create(n, bp.wires);
            visual = visual_owned.get();
        }

        // Skip hidden nodes (for blueprint collapsing)
        if (!visual->isVisible()) {
            node_idx++;
            continue;
        }

        // Check if node is selected
        bool is_selected = false;
        if (selected_nodes) {
            for (size_t idx : *selected_nodes) {
                if (idx == node_idx) { is_selected = true; break; }
            }
        }

        visual->render(dl, vp, canvas_min, is_selected);

        node_idx++;
    }

    // --- Tooltip detection [h1a2b3c4] ---
    if (hover_world_pos && out_tooltip && simulation && simulation->is_running()) {
        constexpr float PORT_RADIUS = 8.0f;
        // Check ports
        for (const auto& n : bp.nodes) {
            // Skip hidden nodes (blueprint collapsing)
            if (!n.visible) continue;
            VisualNode* vis;
            std::unique_ptr<VisualNode> vis_owned;
            if (cache) {
                vis = cache->getOrCreate(n, bp.wires);
            } else {
                vis_owned = VisualNodeFactory::create(n, bp.wires);
                vis = vis_owned.get();
            }
            for (size_t pi = 0; pi < vis->getPortCount(); pi++) {
                auto* port = vis->getPort(pi);
                if (!port) continue;
                Pt port_wpos = port->worldPosition();
                float dx = hover_world_pos->x - port_wpos.x;
                float dy = hover_world_pos->y - port_wpos.y;
                if (dx * dx + dy * dy <= PORT_RADIUS * PORT_RADIUS) {
                    // [f6a8d3e7] Use logical port name for simulation lookup.
                    std::string logical_port = port->logicalName();
                    float val = simulation->get_port_value(n.id, logical_port);
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "%.3f", val);
                    out_tooltip->active = true;
                    out_tooltip->screen_pos = vp.world_to_screen(port_wpos, canvas_min);
                    out_tooltip->label = n.id + "." + logical_port;
                    out_tooltip->text = buf;
                    return;
                }
            }
        }
        // Check wire segments
        for (size_t wi = 0; wi < bp.wires.size(); wi++) {
            const auto& poly = all_polylines[wi];
            for (size_t i = 0; i + 1 < poly.size(); i++) {
                Pt a = poly[i], b = poly[i + 1];
                float seg_len_sq = (b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y);
                if (seg_len_sq < 1e-6f) continue;
                float t = ((hover_world_pos->x - a.x) * (b.x - a.x) + (hover_world_pos->y - a.y) * (b.y - a.y)) / seg_len_sq;
                t = std::max(0.0f, std::min(1.0f, t));
                Pt proj(a.x + t * (b.x - a.x), a.y + t * (b.y - a.y));
                float dx = hover_world_pos->x - proj.x;
                float dy = hover_world_pos->y - proj.y;
                float dist_sq = dx * dx + dy * dy;

                if (dist_sq <= PORT_RADIUS * PORT_RADIUS) {
                    const auto& w = bp.wires[wi];
                    std::string port = w.start.node_id + "." + w.start.port_name;
                    float val = simulation->get_wire_voltage(port);
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "%.3f", val);
                    out_tooltip->active = true;
                    out_tooltip->screen_pos = vp.world_to_screen(proj, canvas_min);
                    out_tooltip->label = port;
                    out_tooltip->text = buf;
                    return;
                }
            }
        }
    }
}

void render_tooltip(IDrawList* dl, const TooltipInfo& tooltip) {
    if (!tooltip.active) return;

    constexpr float FONT_SIZE = 14.0f;
    constexpr float PAD = 4.0f;
    constexpr uint32_t BG_COLOR = 0xCC1A1A1A;    // dark bg
    constexpr uint32_t LABEL_COLOR = 0xFFAAAAAA;  // gray label
    constexpr uint32_t VALUE_COLOR = 0xFFFFFFFF;  // white value

    std::string full = tooltip.label + ": " + tooltip.text;
    Pt text_size = dl->calc_text_size(full.c_str(), FONT_SIZE);

    Pt bg_min(tooltip.screen_pos.x, tooltip.screen_pos.y - text_size.y - PAD * 2);
    Pt bg_max(tooltip.screen_pos.x + text_size.x + PAD * 2, tooltip.screen_pos.y);

    dl->add_rect_filled(bg_min, bg_max, BG_COLOR);
    dl->add_text(Pt(bg_min.x + PAD, bg_min.y + PAD),
                 (tooltip.label + ": ").c_str(), LABEL_COLOR, FONT_SIZE);

    Pt label_size = dl->calc_text_size((tooltip.label + ": ").c_str(), FONT_SIZE);
    dl->add_text(Pt(bg_min.x + PAD + label_size.x, bg_min.y + PAD),
                 tooltip.text.c_str(), VALUE_COLOR, FONT_SIZE);
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
