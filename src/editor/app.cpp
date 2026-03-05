#include "app.h"
#include "hittest.h"
#include "wires/hittest.h"
#include "trigonometry.h"
#include "router/router.h"
#include "visual_node.h"
#include "debug.h"
#include <algorithm>

void EditorApp::on_mouse_down(Pt world_pos, MouseButton btn, Pt canvas_min, bool add_to_selection) {
    (void)canvas_min;

    if (btn == MouseButton::Left) {
        // Hit test - что под курсром?
        HitResult hit = hit_test(blueprint, world_pos, viewport);
        DEBUG_LOG("Hit test at ({:.1f}, {:.1f}): type={}", world_pos.x, world_pos.y, (int)hit.type);

        if (hit.type == HitType::Node) {
            // Если не add_to_selection - очищаем предыдущее выделение
            if (!add_to_selection) {
                interaction.clear_selection();
            }
            // Выделяем узел и начинаем drag
            interaction.add_node_selection(hit.node_index);
            // drag_anchor = позиция первого выделенного узла (unsnapped accumulator)
            // Use IDraggable interface to get position
            auto primary_visual = VisualNodeFactory::create(blueprint.nodes[hit.node_index], blueprint.wires);
            Pt primary_pos = primary_visual->getPosition();
            interaction.start_drag_node(primary_pos);
            // Сохраняем смещения относительно anchor для каждого выделенного узла
            interaction.drag_node_offsets.clear();
            for (size_t idx : interaction.selected_nodes) {
                if (idx < blueprint.nodes.size()) {
                    auto visual = VisualNodeFactory::create(blueprint.nodes[idx], blueprint.wires);
                    interaction.drag_node_offsets.push_back(visual->getPosition() - primary_pos);
                }
            }
        } else if (hit.type == HitType::RoutingPoint) {
            // Выделяем routing point и начинаем drag
            Pt rp_pos = blueprint.wires[hit.wire_index].routing_points[hit.routing_point_index];
            interaction.start_drag_routing_point(hit.wire_index, hit.routing_point_index, rp_pos);
        } else if (hit.type == HitType::Wire) {
            // Выделяем провод
            interaction.selected_wire = hit.wire_index;
            interaction.clear_drag();
        } else {
            // Клик в пустоту - panning (как в Rust)
            interaction.clear_selection();
            interaction.set_panning(true);
        }
    } else if (btn == MouseButton::Right) {
        // Right click - show context menu
        HitResult hit = hit_test(blueprint, world_pos, viewport);
        if (hit.type == HitType::None) {
            // Click on empty space - show add component menu
            show_context_menu = true;
            context_menu_pos = world_pos;
        }
        // For other hit types, we could show context menus later (delete, properties, etc.)
    }
}

void EditorApp::on_mouse_up(MouseButton btn) {
    if (btn == MouseButton::Left) {
        // Если был marquee selection - применить его
        if (interaction.marquee_selecting) {
            // Marquee selection - выделить все узлы в прямоугольнике
            float min_x = std::min(interaction.marquee_start.x, interaction.marquee_end.x);
            float max_x = std::max(interaction.marquee_start.x, interaction.marquee_end.x);
            float min_y = std::min(interaction.marquee_start.y, interaction.marquee_end.y);
            float max_y = std::max(interaction.marquee_start.y, interaction.marquee_end.y);

            for (size_t i = 0; i < blueprint.nodes.size(); i++) {
                auto visual = VisualNodeFactory::create(blueprint.nodes[i], blueprint.wires);
                Pt pos = visual->getPosition();
                Pt size = visual->getSize();
                // Check if node center is in marquee
                float cx = pos.x + size.x / 2;
                float cy = pos.y + size.y / 2;
                if (cx >= min_x && cx <= max_x && cy >= min_y && cy <= max_y) {
                    interaction.add_node_selection(i);
                }
            }
            interaction.marquee_selecting = false;
        }

        // Snap уже применяется в on_mouse_drag, не нужен на release
        interaction.drag_node_offsets.clear();
        interaction.set_panning(false);
        interaction.end_drag();
    }
}

void EditorApp::on_mouse_drag(Pt world_delta, Pt canvas_min) {
    (void)canvas_min;

    if (interaction.panning) {
        // Панорамирование - обратный delta
        viewport.pan.x -= world_delta.x;
        viewport.pan.y -= world_delta.y;
    } else if (interaction.dragging == Dragging::Node) {
        // Accumulate unsnapped delta, then snap
        interaction.update_drag_anchor(world_delta);
        Pt snapped = editor_math::snap_to_grid(interaction.drag_anchor, blueprint.grid_step);
        for (size_t i = 0; i < interaction.selected_nodes.size(); i++) {
            size_t idx = interaction.selected_nodes[i];
            if (idx < blueprint.nodes.size()) {
                Pt offset = (i < interaction.drag_node_offsets.size())
                    ? interaction.drag_node_offsets[i] : Pt(0.0f, 0.0f);
                Pt new_pos = snapped + offset;
                // Use IDraggable interface to set position
                auto visual = VisualNodeFactory::create(blueprint.nodes[idx], blueprint.wires);
                visual->setPosition(new_pos);
                // Also update raw Node for persistence
                blueprint.nodes[idx].pos = new_pos;
            }
        }
    } else if (interaction.dragging == Dragging::RoutingPoint) {
        // Accumulate unsnapped delta, then snap
        interaction.update_drag_anchor(world_delta);
        Pt snapped = editor_math::snap_to_grid(interaction.drag_anchor, blueprint.grid_step);
        size_t wire_idx = interaction.routing_point_wire;
        size_t rp_idx = interaction.routing_point_index;
        if (wire_idx < blueprint.wires.size()) {
            auto& wire = blueprint.wires[wire_idx];
            if (rp_idx < wire.routing_points.size()) {
                wire.routing_points[rp_idx] = snapped;
            }
        }
    } else if (interaction.marquee_selecting) {
        // Обновляем конец marquee
        interaction.marquee_end = interaction.marquee_end + world_delta;
    }
}

void EditorApp::on_scroll(float delta, Pt mouse_pos, Pt canvas_min) {
    viewport.zoom_at(delta, mouse_pos, canvas_min);
}

void EditorApp::on_key_down(Key key) {
    if (key == Key::Escape) {
        // Сброс выделения
        interaction.clear_selection();
    } else if (key == Key::Space) {
        // Toggle simulation
        if (simulation_running) {
            stop_simulation();
        } else {
            start_simulation();
        }
    } else if (key == Key::Delete) {
        // Удаление всех выделенных узлов (с конца чтобы не сбить индексы)
        std::sort(interaction.selected_nodes.begin(), interaction.selected_nodes.end(), std::greater<size_t>());

        // Collect IDs of nodes to delete
        std::vector<std::string> deleted_ids;
        for (size_t idx : interaction.selected_nodes) {
            if (idx < blueprint.nodes.size()) {
                deleted_ids.push_back(blueprint.nodes[idx].id);
                blueprint.nodes.erase(blueprint.nodes.begin() + idx);
            }
        }

        // Remove wires that reference deleted nodes
        blueprint.wires.erase(
            std::remove_if(blueprint.wires.begin(), blueprint.wires.end(),
                [&deleted_ids](const Wire& w) {
                    for (const auto& id : deleted_ids) {
                        if (w.start.node_id == id || w.end.node_id == id) return true;
                    }
                    return false;
                }),
            blueprint.wires.end());

        interaction.clear_selection();

        // Rebuild simulation after removing nodes
        rebuild_simulation();
    } else if (key == Key::R) {
        // R - роутинг выделенного провода
        if (interaction.selected_wire.has_value()) {
            size_t wire_idx = *interaction.selected_wire;
            if (wire_idx < blueprint.wires.size()) {
                Wire& wire = blueprint.wires[wire_idx];

                // Находим start и end node
                const Node* start_node = nullptr;
                const Node* end_node = nullptr;
                for (const auto& n : blueprint.nodes) {
                    if (n.id == wire.start.node_id) start_node = &n;
                    if (n.id == wire.end.node_id) end_node = &n;
                }

                if (start_node && end_node) {
                    Pt start_pos = editor_math::get_port_position(*start_node, wire.start.port_name.c_str(), blueprint.wires);
                    Pt end_pos = editor_math::get_port_position(*end_node, wire.end.port_name.c_str(), blueprint.wires);

                    // Build existing wire polylines (all wires except current)
                    std::vector<std::vector<Pt>> existing_paths;
                    for (size_t i = 0; i < blueprint.wires.size(); i++) {
                        if (i == wire_idx) continue;
                        const auto& ow = blueprint.wires[i];
                        const Node* sn = nullptr;
                        const Node* en = nullptr;
                        for (const auto& n : blueprint.nodes) {
                            if (n.id == ow.start.node_id) sn = &n;
                            if (n.id == ow.end.node_id) en = &n;
                        }
                        if (!sn || !en) continue;
                        std::vector<Pt> poly;
                        poly.push_back(editor_math::get_port_position(*sn, ow.start.port_name.c_str(), blueprint.wires));
                        poly.insert(poly.end(), ow.routing_points.begin(), ow.routing_points.end());
                        poly.push_back(editor_math::get_port_position(*en, ow.end.port_name.c_str(), blueprint.wires));
                        existing_paths.push_back(std::move(poly));
                    }

                    // Роутинг с port departure/arrival + existing wire avoidance
                    auto path = route_around_nodes(
                        start_pos, end_pos,
                        *start_node, wire.start.port_name.c_str(),
                        *end_node, wire.end.port_name.c_str(),
                        blueprint.nodes, blueprint.grid_step,
                        existing_paths);

                    if (!path.empty()) {
                        // Strip first and last points (they are port positions, not routing points)
                        // routing_points = intermediate waypoints only
                        if (path.size() > 2) {
                            wire.routing_points.assign(path.begin() + 1, path.end() - 1);
                        } else {
                            wire.routing_points.clear();
                        }
                        DEBUG_LOG("Rerouted wire {} with {} points", wire.id, wire.routing_points.size());
                    } else {
                        DEBUG_LOG("Could not find A* route for wire {}", wire.id);
                    }
                }
            }
        }
    }
}

void EditorApp::on_double_click(Pt world_pos) {
    DEBUG_LOG("Double click at ({:.1f}, {:.1f})", world_pos.x, world_pos.y);

    // 1. Сначала проверяем hit на routing point - удалить
    auto rp_hit = hit_test_routing_point(blueprint, world_pos);
    if (rp_hit) {
        DEBUG_LOG("Double click: delete routing point wire={} rp={}", rp_hit->wire_index, rp_hit->routing_point_index);
        if (rp_hit->wire_index < blueprint.wires.size()) {
            auto& wire = blueprint.wires[rp_hit->wire_index];
            if (rp_hit->routing_point_index < wire.routing_points.size()) {
                wire.routing_points.erase(wire.routing_points.begin() + rp_hit->routing_point_index);
            }
        }
        return;
    }

    // 2. Потом проверяем hit на wire - добавить новую точку
    HitResult hit = hit_test(blueprint, world_pos, viewport);
    DEBUG_LOG("Double click: hit type={}", (int)hit.type);
    if (hit.type == HitType::Wire) {
        DEBUG_LOG("Double click: add routing point to wire {}", hit.wire_index);
        if (hit.wire_index < blueprint.wires.size()) {
            auto& wire = blueprint.wires[hit.wire_index];

            // Находим ближайший сегмент и вставляем после него
            const Node* start_node = nullptr;
            const Node* end_node = nullptr;
            for (const auto& n : blueprint.nodes) {
                if (n.id == wire.start.node_id) start_node = &n;
                if (n.id == wire.end.node_id) end_node = &n;
            }
            if (!start_node || !end_node) return;

            Pt start_pos = editor_math::get_port_position(*start_node, wire.start.port_name.c_str(), blueprint.wires);
            Pt end_pos = editor_math::get_port_position(*end_node, wire.end.port_name.c_str(), blueprint.wires);

            // Ищем ближайший сегмент для вставки
            size_t insert_idx = 0;
            float min_dist = editor_math::distance_to_segment(world_pos, start_pos, end_pos);

            // Проверяем сегменты: start -> rp[0] -> ... -> rp[n] -> end
            Pt prev = start_pos;
            for (size_t i = 0; i <= wire.routing_points.size(); i++) {
                Pt next = (i < wire.routing_points.size()) ? wire.routing_points[i] : end_pos;
                float dist = editor_math::distance_to_segment(world_pos, prev, next);
                if (dist < min_dist) {
                    min_dist = dist;
                    insert_idx = i; // вставить перед next
                }
                prev = next;
            }

            // Вставляем с привязкой к сетке
            Pt snapped_pos = editor_math::snap_to_grid(world_pos, blueprint.grid_step);
            wire.routing_points.insert(wire.routing_points.begin() + insert_idx, snapped_pos);
        }
    }
}

void EditorApp::update_node_content_from_simulation() {
    if (!simulation_running) return;

    for (auto& node : blueprint.nodes) {
        // Update Battery gauge voltage
        if (node.type_name == "Battery") {
            float voltage = simulation.get_port_value(node.id, "v_out");
            node.node_content.value = voltage;
        }
        // Update IndicatorLight text based on brightness
        else if (node.type_name == "IndicatorLight") {
            float brightness = simulation.get_port_value(node.id, "brightness");
            node.node_content.label = (brightness > 0.1f) ? "ON" : "OFF";
        }
        // Update DMR400 state
        else if (node.type_name == "DMR400") {
            float v_gen = simulation.get_port_value(node.id, "v_gen");
            float v_bus = simulation.get_port_value(node.id, "v_bus");
            // Relay is closed (ON) when generator voltage is higher than bus
            // This is simplified - actual logic has hysteresis
            bool connected = v_gen > v_bus + 2.0f;
            node.node_content.state = connected;
        }
        // Update Switch toggle state from state port (1.0V = closed, 0.0V = open)
        else if (node.type_name == "Switch") {
            float state_voltage = simulation.get_port_value(node.id, "state");
            node.node_content.state = (state_voltage > 0.5f);
        }
        // Update HoldButton state from state port (1.0V = pressed, 0.0V = released/idle)
        else if (node.type_name == "HoldButton") {
            float state_voltage = simulation.get_port_value(node.id, "state");
            // state=true = PRESSED, state=false = RELEASED/idle
            node.node_content.state = (state_voltage > 0.5f);
        }
        // Relay has no UI - automatic device controlled by external signals
    }
}

// Helper: create default node_content based on ComponentDefinition
static NodeContent create_node_content(const an24::ComponentDefinition* def) {
    using namespace an24;

    NodeContent content;
    content.type = NodeContentType::None;

    if (!def) return content;

    // Parse content type from component definition
    std::string content_type_str = def->default_content_type;

    if (content_type_str == "Gauge") {
        content.type = NodeContentType::Gauge;
        content.label = "V";
        content.value = 0.0f;
        content.min = 0.0f;
        content.max = 30.0f;
        content.unit = "V";
    } else if (content_type_str == "Switch") {
        content.type = NodeContentType::Switch;
        content.label = "ON";
        // Read default "closed" state from component definition
        auto it = def->default_params.find("closed");
        if (it != def->default_params.end()) {
            content.state = (it->second == "true");
        } else {
            content.state = false;  // Default to false if not specified
        }
    } else if (content_type_str == "HoldButton") {
        content.type = NodeContentType::Switch;  // Reuse Switch UI (button)
        content.label = "RELEASED";
        content.state = false;  // Default to released state
    } else if (content_type_str == "Text") {
        content.type = NodeContentType::Text;
        content.label = "OFF";
    }

    return content;
}

void EditorApp::add_component(const std::string& classname, Pt world_pos) {
    using namespace an24;
    
    // Check if component exists in registry
    if (!component_registry.has(classname)) {
        printf("Error: Unknown component classname '%s'\n", classname.c_str());
        return;
    }
    
    const auto* def = component_registry.get(classname);
    if (!def) {
        printf("Error: Component definition not found for '%s'\n", classname.c_str());
        return;
    }
    
    // Generate unique ID
    int counter = 1;
    std::string base_id = classname;
    // Convert to lowercase for ID (e.g., "Battery" -> "battery")
    std::transform(base_id.begin(), base_id.end(), base_id.begin(), ::tolower);
    
    std::string unique_id;
    do {
        unique_id = base_id + "_" + std::to_string(counter++);
    } while (blueprint.find_node(unique_id.c_str()) != nullptr);
    
    // Snap position to grid
    Pt snapped_pos = editor_math::snap_to_grid(world_pos, blueprint.grid_step);
    
    // Create node
    Node node;
    node.id = unique_id;
    node.name = unique_id;  // Use ID as display name for now
    node.type_name = classname;
    node.pos = snapped_pos;
    
    // [a1b2] Set NodeKind and size based on classname
    if (classname == "Bus") {
        node.kind = NodeKind::Bus;
        node.size = Pt(40.0f, 40.0f);
    } else if (classname == "RefNode") {
        node.kind = NodeKind::Ref;
        node.size = Pt(40.0f, 40.0f);
    } else {
        node.kind = NodeKind::Node;
        node.size = Pt(120.0f, 80.0f);
    }
    
    // Add ports from component definition
    for (const auto& [port_name, port_def] : def->default_ports) {
        if (port_def.direction == PortDirection::In) {
            node.inputs.emplace_back(port_name.c_str(), PortSide::Input);
        } else {
            node.outputs.emplace_back(port_name.c_str(), PortSide::Output);
        }
    }

    // Create node_content from component definition (no more hardcoded component lists!)
    node.node_content = create_node_content(def);

    // Add node to blueprint
    blueprint.add_node(node);

    // Clear visual cache to force rebuild
    visual_cache.clear();

    // Rebuild simulation to include new component
    rebuild_simulation();

    printf("Added component: %s (id=%s) at (%.1f, %.1f) with %zu inputs, %zu outputs\n",
           classname.c_str(), unique_id.c_str(), snapped_pos.x, snapped_pos.y,
           node.inputs.size(), node.outputs.size());
}

void EditorApp::trigger_switch(const std::string& node_id) {
    // Toggle control value: 0.0 → 1.0 → 0.0 → 1.0 (Level Toggle pattern)
    float current = simulation.get_port_value(node_id, "control");
    float next = (current < 0.5f) ? 1.0f : 0.0f;

    std::string control_port = node_id + ".control";
    signal_overrides[control_port] = next;

    printf("Trigger switch: %s.control = %.1f (was %.1f)\n", node_id.c_str(), next, current);
}

void EditorApp::hold_button_press(const std::string& node_id) {
    // Add to held buttons set - control=1.0V will be sent every frame
    held_buttons.insert(node_id);
}

void EditorApp::hold_button_release(const std::string& node_id) {
    // Remove from held buttons set
    held_buttons.erase(node_id);

    // Send 2.0V release signal this frame
    std::string control_port = node_id + ".control";
    signal_overrides[control_port] = 2.0f;
}

void EditorApp::update_simulation_step() {
    if (!simulation_running) return;

    // Send control=1.0V for all currently held HoldButtons
    for (const auto& node_id : held_buttons) {
        std::string control_port = node_id + ".control";
        signal_overrides[control_port] = 1.0f;
    }

    // Apply signal overrides (button clicks, etc.)
    simulation.apply_overrides(signal_overrides);

    // Run simulation step
    simulation.step(simulation.dt);

    // Clear overrides map (signals stay as set by components)
    signal_overrides.clear();
}
