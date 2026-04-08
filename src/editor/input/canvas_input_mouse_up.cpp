#include "input/canvas_input.h"
#include "visual/scene.h"
#include "visual/scene_hittest.h"
#include "visual/scene_mutations.h"
#include "visual/snap.h"
#include "visual/node/visual_node.h"
#include "visual/node/ref_node_widget.h"
#include "visual/widgets/content_widgets.h"
#include "visual/wire/wire.h"
#include "visual/wire/routing_point.h"
#include "viewport/viewport.h"
#include "commands/commands.h"
#include "canvas_input_internal.h"
#include <algorithm>
#include <unordered_set>

using namespace canvas_input_impl;

void CanvasInput::commit_drag_node() {
    auto nodes = selected_nodes();
    bool any_moved = false;
    std::vector<ui::InternedId> moved_node_ids;
    for (size_t i = 0; i < nodes.size() && i < drag_initial_positions_.size(); ++i) {
        if (nodes[i]->worldPos() != drag_initial_positions_[i]) {
            any_moved = true;
            moved_node_ids.push_back(interner_.intern(nodes[i]->id()));
        }
    }
    if (!any_moved) return;
    for (auto* widget : nodes) {
        ui::InternedId node_iid = interner_.intern(widget->id());
        if (!node_iid.empty()) {
            execute(model_, interner_, cmd_move_node(node_iid, widget->worldPos().x, widget->worldPos().y));
        }
    }

    std::unordered_set<ui::InternedId> nodes_to_orient;
    for (ui::InternedId id : moved_node_ids) {
        nodes_to_orient.insert(id);
    }

    std::unordered_map<ui::InternedId, ui::InternedId> ref_to_connected;
    for (const bp2::Blueprint::Wire& w : model_.current().wires()) {
        auto [src_node, _sp] = editor_math::path_to_node_port(w.source, arena_);
        auto [tgt_node, _tp] = editor_math::path_to_node_port(w.target, arena_);
        if (src_node.empty() || tgt_node.empty()) continue;

         const bp2::Blueprint::Node* src_n = model_.current().find_node(src_node);
         const bp2::Blueprint::Node* tgt_n = model_.current().find_node(tgt_node);
         if (!src_n || !tgt_n) continue;

         if (src_n->view.render_hint == "ref" && ref_to_connected.count(src_node) == 0) {
             ref_to_connected.emplace(src_node, tgt_node);
         }
         if (tgt_n->view.render_hint == "ref" && ref_to_connected.count(tgt_node) == 0) {
             ref_to_connected.emplace(tgt_node, src_node);
         }

        if (nodes_to_orient.count(src_node)) nodes_to_orient.insert(tgt_node);
        if (nodes_to_orient.count(tgt_node)) nodes_to_orient.insert(src_node);
    }

    for (const auto& [ref_id, connected_id] : ref_to_connected) {
        orient_ref_node_port_impl(ref_id, connected_id);
    }
    for (ui::InternedId id : moved_node_ids) {
        const bp2::Blueprint::Node* n = model_.current().find_node(id);
        if (n && n->view.render_hint == "ref" && ref_to_connected.count(id) == 0) {
            orient_ref_node_port_by_wire_scan(id);
        }
    }

    debug_validate_command_boundary(model_, interner_, arena_, parser_registry_);
}

bool CanvasInput::orient_ref_node_port_impl(ui::InternedId ref_id, ui::InternedId connected_id) {
    auto* ref_widget = dynamic_cast<visual::RefNodeWidget*>(resolve_node(ref_id));
    auto* other_widget = resolve_node(connected_id);
    if (!ref_widget || !other_widget) return false;

    const Pt ref_pos = ref_widget->worldPos();
    const Pt ref_size = ref_widget->size();
    const Pt other_pos = other_widget->worldPos();
    const Pt other_size = other_widget->size();

    const Pt ref_center(ref_pos.x + ref_size.x * 0.5f, ref_pos.y + ref_size.y * 0.5f);
    const Pt other_center(other_pos.x + other_size.x * 0.5f,
                          other_pos.y + other_size.y * 0.5f);

    ref_widget->setPortLayoutSide(editor_math::side_from_relative_position(ref_center, other_center));
    return true;
}

void CanvasInput::orient_ref_node_port_by_wire_scan(ui::InternedId ref_node_id) {
    if (ref_node_id.empty()) return;
    const bp2::Blueprint::Node* ref_node = model_.current().find_node(ref_node_id);
    if (!ref_node || ref_node->layout.group_id != group_id_ || ref_node->view.render_hint != "ref") return;

    ui::InternedId connected_node_id;
    for (const bp2::Blueprint::Wire& w : model_.current().wires()) {
        auto [src_node, _sp] = editor_math::path_to_node_port(w.source, arena_);
        auto [tgt_node, _tp] = editor_math::path_to_node_port(w.target, arena_);
        if (src_node == ref_node_id) { connected_node_id = tgt_node; break; }
        if (tgt_node == ref_node_id) { connected_node_id = src_node; break; }
    }
    if (connected_node_id.empty()) return;
    orient_ref_node_port_impl(ref_node_id, connected_node_id);
}

void CanvasInput::commit_drag_routing_point() {
    const bp2::Blueprint::Wire* bp2_wire = model_.current().find_wire(rp_wire_id_);
    if (!bp2_wire) return;
    Pt final_pos = rp_point_ ? rp_point_->worldPos() : Pt(0, 0);

    std::vector<std::pair<float,float>> new_points;
    new_points.reserve(rp_initial_points_.size());
    for (const auto& pt : rp_initial_points_)
        new_points.push_back({pt.x, pt.y});
    if (rp_index_ < new_points.size()) {
        new_points[rp_index_] = {final_pos.x, final_pos.y};
    }

    bool changed = false;
    if (new_points.size() == rp_initial_points_.size()) {
        for (size_t i = 0; i < new_points.size(); ++i) {
            if (new_points[i].first  != rp_initial_points_[i].x ||
                new_points[i].second != rp_initial_points_[i].y) {
                changed = true;
                break;
            }
        }
    } else {
        changed = true;
    }

    if (!changed || rp_wire_id_.empty()) return;
    execute(model_, interner_, cmd_set_routing_points(rp_wire_id_, std::move(new_points)));
    debug_validate_command_boundary(model_, interner_, arena_, parser_registry_);
}

void CanvasInput::commit_resize_node() {
    auto* resize_widget = resolve_node(resize_widget_id_);
    if (!resize_widget) return;
    Pt new_pos = resize_widget->worldPos();
    Pt new_size = resize_widget->size();
    ui::InternedId node_iid = resize_widget_id_;
    if ((new_pos == resize_original_pos_ && new_size == resize_original_size_) || node_iid.empty()) return;
    execute(model_, interner_, cmd_resize_node(node_iid, new_pos.x, new_pos.y, new_size.x, new_size.y));
    debug_validate_command_boundary(model_, interner_, arena_, parser_registry_);
}

InputResult CanvasInput::on_mouse_up(MouseButton btn, Pt screen_pos, Pt canvas_min) {
    InputResult result;

    if (btn == MouseButton::Left) {
        switch (state_) {
            case InputState::ReconnectingWire:
                result = finish_wire_reconnection(screen_pos, canvas_min);
                break;

            case InputState::CreatingWire:
                result = finish_wire_creation(screen_pos, canvas_min);
                break;

            case InputState::MarqueeSelect:
                finish_marquee();
                break;

            case InputState::DraggingNode:
                commit_drag_node();
                break;

            case InputState::DraggingRoutingPoint:
                commit_drag_routing_point();
                break;

            case InputState::ResizingNode:
                commit_resize_node();
                break;

            default:
                break;
        }
        leave_state();
    }
    return result;
}

InputResult CanvasInput::on_double_click(Pt screen_pos, Pt canvas_min) {
    InputResult result;
    Pt world = viewport_.screen_to_world(screen_pos, canvas_min);

    auto hit = visual::hit_test(scene_, world);

    if (!read_only && !simulation_mode) {
        if (auto* hrp = std::get_if<visual::HitRoutingPoint>(&hit)) {
            ui::InternedId wire_iid = interner_.intern(hrp->wire->id());
            const bp2::Blueprint::Wire* bp2_wire = model_.current().find_wire(wire_iid);
            if (bp2_wire && hrp->index < bp2_wire->routing_points.size()) {
                auto new_points = bp2_wire->routing_points;
                new_points.erase(new_points.begin() + static_cast<long>(hrp->index));

                if (!wire_iid.empty()) {
                    snapshot_and_execute(cmd_set_routing_points(wire_iid, std::move(new_points)));
                    if (hrp->wire) {
                        hrp->wire->removeRoutingPoint(hrp->index);
                    }
                }
            }
            return result;
        }
    }

    if (auto* hn = std::get_if<visual::HitNode>(&hit)) {
         std::string node_id(hn->widget->id());
         ui::InternedId node_iid = interner_.lookup(node_id);
         const bp2::Blueprint::Node* node = node_iid.empty() ? nullptr : model_.current().find_node(node_iid);
         if (!read_only && !simulation_mode && node && std::string(interner_.resolve(node->semantic.type)) == "Value") {
             result.open_inline_value_editor = true;
             result.inline_value_editor_node_id = node_id;
             result.has_inline_value_editor_screen_pos = true;
             result.inline_value_editor_screen_pos = screen_pos;
             return result;
         }
         if (node && node->view.expandable) {
             result.open_sub_window = node_id;
             return result;
         }
    }

    if (!read_only && !simulation_mode) {
        if (auto* hw = std::get_if<visual::HitWire>(&hit)) {
            ui::InternedId wire_iid = interner_.intern(hw->wire->id());
            const bp2::Blueprint::Wire* bp2_wire = model_.current().find_wire(wire_iid);
            if (bp2_wire) {
                auto new_points = bp2_wire->routing_points;
                Pt snapped = editor_math::snap_to_grid(world, viewport_.grid_step);
                size_t insert_idx = hw->segment;
                new_points.insert(new_points.begin() + static_cast<long>(insert_idx), {snapped.x, snapped.y});

                if (!wire_iid.empty()) {
                    snapshot_and_execute(cmd_set_routing_points(wire_iid, std::move(new_points)));
                    if (hw->wire) {
                        hw->wire->addRoutingPoint(snapped, insert_idx);
                    }
                }
            }
        }
    }

    return result;
}

InputResult CanvasInput::on_key(Key key) {
    InputResult result;

    if (read_only || simulation_mode) {
        if (key == Key::Escape) clear_selection();
        return result;
    }

    switch (key) {
        case Key::Escape:
            if (state_ != InputState::Idle && state_ != InputState::Panning) {
                bool needs_rebuild =
                    state_ == InputState::DraggingNode ||
                    state_ == InputState::DraggingRoutingPoint ||
                    state_ == InputState::ResizingNode;
                cancel_gesture();
                if (needs_rebuild) {
                    visual::mutations::rebuild(scene_, model_.current(), interner_, arena_, group_id_);
                }
            }
            clear_selection();
            break;

        case Key::Delete:
        case Key::Backspace: {
            if (selected_node_ids_.empty()) break;

            model_.push_checkpoint();
            for (const auto& nid : selected_node_ids_) {
                if (!nid.empty()) {
                    std::vector<ui::InternedId> connected_wires;
                    connected_wires.reserve(model_.current().wires().size());
                    for (const auto& w : model_.current().wires()) {
                        auto [src_node, _src_port] = editor_math::path_to_node_port(w.source, arena_);
                        auto [tgt_node, _tgt_port] = editor_math::path_to_node_port(w.target, arena_);
                        if (src_node == nid || tgt_node == nid) {
                            connected_wires.push_back(w.id);
                        }
                    }
                    execute(model_, interner_, cmd_remove_node(nid, std::move(connected_wires)));
                }
            }
            debug_validate_command_boundary(model_, interner_, arena_, parser_registry_);
            hovered_routing_point_ = nullptr;
            visual::mutations::rebuild(scene_, model_.current(), interner_, arena_, group_id_);
            clear_selection();
            result.rebuild_simulation = true;
            break;
        }

        case Key::RightBracket: {
            viewport_.grid_step_up();
            float new_step = viewport_.grid_step;
            snapshot_and_execute(cmd_set_grid_step(new_step));
            break;
        }

        case Key::LeftBracket: {
            viewport_.grid_step_down();
            float new_step = viewport_.grid_step;
            snapshot_and_execute(cmd_set_grid_step(new_step));
            break;
        }

        default:
            break;
    }
    return result;
}

void CanvasInput::finish_marquee() {
    float min_x = std::min(marquee_start_.x, marquee_end_.x);
    float max_x = std::max(marquee_start_.x, marquee_end_.x);
    float min_y = std::min(marquee_start_.y, marquee_end_.y);
    float max_y = std::max(marquee_start_.y, marquee_end_.y);

    for (const auto& root : scene_.roots()) {
        auto* vroot = static_cast<visual::Widget*>(root.get());
        if (vroot->renderLayer() == visual::RenderLayer::Wire) continue;
        if (vroot->id().empty()) continue;

         ui::InternedId node_iid = interner_.lookup(std::string_view(vroot->id()));
         const bp2::Blueprint::Node* node = node_iid.empty() ? nullptr : model_.current().find_node(node_iid);
         if (!node || node->layout.group_id != group_id_) continue;

        Pt pos = vroot->worldPos();
        Pt sz = vroot->size();
        float cx = pos.x + sz.x / 2;
        float cy = pos.y + sz.y / 2;
        if (cx >= min_x && cx <= max_x && cy >= min_y && cy <= max_y)
            add_node_selection(vroot);
    }
}
