#include "input/canvas_input.h"
#include "visual/scene.h"
#include "visual/scene_mutations.h"
#include "visual/snap.h"
#include "visual/node/visual_node.h"
#include "visual/node/ref_node_widget.h"
#include "visual/wire/wire.h"
#include "visual/wire/routing_point.h"
#include "viewport/viewport.h"
#include "commands/commands.h"
#include "canvas_input_internal.h"
#include <algorithm>
#include <unordered_set>

using namespace canvas_input_impl;

void CanvasInput::commit_drag_node() {
     // drag_current_positions_ is populated by handle_drag_node on each drag event.
     // If empty, no drag movement occurred (click-without-drag) — nothing to commit.
     if (drag_current_positions_.empty()) return;

     bool any_moved = false;
     std::vector<ui::InternedId> moved_node_ids;
     size_t drag_idx = 0;
     for (const auto& node_id : selected_node_ids()) {
         if (node_id.empty() || !host_.find_node(node_id)) continue;
         if (drag_idx < drag_current_positions_.size() &&
             drag_idx < drag_initial_positions_.size() &&
             drag_current_positions_[drag_idx] != drag_initial_positions_[drag_idx]) {
             any_moved = true;
             moved_node_ids.push_back(node_id);
         }
         ++drag_idx;
     }
     if (!any_moved) return;
     host_.mutate_atomically([&] {
         size_t pos_idx = 0;
         for (const auto& node_id : selected_node_ids()) {
             if (node_id.empty() || !host_.find_node(node_id)) continue;
             if (pos_idx < drag_current_positions_.size()) {
                 host_.update_node_position(node_id,
                     drag_current_positions_[pos_idx].x,
                     drag_current_positions_[pos_idx].y);
             }
             ++pos_idx;
          }
      });

     std::unordered_set<ui::InternedId> nodes_to_orient;
     for (ui::InternedId id : moved_node_ids) {
         nodes_to_orient.insert(id);
     }

     std::unordered_map<ui::InternedId, ui::InternedId> ref_to_connected;
     for (const bp2::Blueprint::Wire& w : host_.wires()) {
         auto src_node = w.source.node;
         auto tgt_node = w.target.node;

          const bp2::Blueprint::Node* src_n = host_.find_node(src_node);
          const bp2::Blueprint::Node* tgt_n = host_.find_node(tgt_node);
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
         const bp2::Blueprint::Node* n = host_.find_node(id);
         if (n && n->view.render_hint == "ref" && ref_to_connected.count(id) == 0) {
             orient_ref_node_port_by_wire_scan(id);
         }
     }

     debug_validate_command_boundary(host_.current_blueprint(), interner_, arena_, parser_registry_);
}

bool CanvasInput::orient_ref_node_port_impl(ui::InternedId ref_id, ui::InternedId connected_id) {
    const bp2::Blueprint::Node* ref_node = host_.find_node(ref_id);
    const bp2::Blueprint::Node* other_node = host_.find_node(connected_id);
    if (!ref_node || !other_node) return false;

    // Use blueprint layout data for center computation.
    // Width/height default to a nominal size when not explicitly set.
    constexpr float DEFAULT_W = 64.0f;
    constexpr float DEFAULT_H = 32.0f;
    const float ref_w = ref_node->layout.width.value_or(DEFAULT_W);
    const float ref_h = ref_node->layout.height.value_or(DEFAULT_H);
    const float other_w = other_node->layout.width.value_or(DEFAULT_W);
    const float other_h = other_node->layout.height.value_or(DEFAULT_H);

    const Pt ref_center(ref_node->layout.x + ref_w * 0.5f,
                        ref_node->layout.y + ref_h * 0.5f);
    const Pt other_center(other_node->layout.x + other_w * 0.5f,
                          other_node->layout.y + other_h * 0.5f);

    auto side = editor_math::side_from_relative_position(ref_center, other_center);

    // Push the layout side to the widget for live preview.
    auto* ref_widget = dynamic_cast<visual::RefNodeWidget*>(resolve_node(ref_id));
    if (ref_widget) {
        ref_widget->setPortLayoutSide(side);
    }
    return true;
}

void CanvasInput::orient_ref_node_port_by_wire_scan(ui::InternedId ref_node_id) {
     if (ref_node_id.empty()) return;
     const bp2::Blueprint::Node* ref_node = host_.find_node(ref_node_id);
     if (!ref_node || ref_node->view.render_hint != "ref") return;

     ui::InternedId connected_node_id;
     for (const bp2::Blueprint::Wire& w : host_.wires()) {
         auto [src_node, _sp] = editor_math::path_to_node_port(w.source, arena_);
         auto [tgt_node, _tp] = editor_math::path_to_node_port(w.target, arena_);
         if (src_node == ref_node_id) { connected_node_id = tgt_node; break; }
         if (tgt_node == ref_node_id) { connected_node_id = src_node; break; }
     }
     if (connected_node_id.empty()) return;
     orient_ref_node_port_impl(ref_node_id, connected_node_id);
}

void CanvasInput::commit_drag_routing_point() {
     const bp2::Blueprint::Wire* bp2_wire = host_.find_wire(rp_wire_id_);
     if (!bp2_wire) return;
     Pt final_pos = rp_drag_pos_;

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
     snapshot_and_execute(cmd_set_routing_points(rp_wire_id_, std::move(new_points)));
}

void CanvasInput::commit_resize_node() {
     Pt new_pos = resize_current_pos_;
     Pt new_size = resize_current_size_;
     ui::InternedId node_iid = resize_widget_id_;
     if ((new_pos == resize_original_pos_ && new_size == resize_original_size_) || node_iid.empty()) return;
     host_.update_node(node_iid, [&](bp2::Blueprint::Node& n) {
         n.layout.x = new_pos.x;
         n.layout.y = new_pos.y;
         n.layout.width = new_size.x;
         n.layout.height = new_size.y;
         n.layout.manual_size = true;
     });
}

InputResult CanvasInput::on_mouse_up(MouseButton btn, Pt screen_pos, Pt canvas_min) {
    InputResult result;

    if (btn == MouseButton::Left) {
         Pt world = viewport_.screen_to_world(screen_pos, canvas_min);
         if (state_uses_semantic_control_session()) {
             last_world_pos_ = world;
             Pt semantic_point = world;
             if (semantic_session_seed_) {
                 semantic_point = Pt(world.x - semantic_session_seed_->node_world_pos.x,
                                     world.y - semantic_session_seed_->node_world_pos.y);
             }
             editor::presentation::SemanticCanvasControllerResult semantic =
                 semantic_canvas_controller_.on_pointer_release(semantic_point);
             publish_semantic_control_result(semantic, result);
         }

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

            case InputState::DraggingSlider:
            case InputState::DraggingKnob:
                break;

            default:
                break;
        }
        leave_state();
        rebuild_snapshot();
    }
    return result;
}

InputResult CanvasInput::on_double_click(Pt screen_pos, Pt canvas_min) {
    InputResult result;
    Pt world = viewport_.screen_to_world(screen_pos, canvas_min);

    auto hit = editor::presentation::hit_test_canvas_scene(snapshot_, world, interner_);

    if (!read_only && !simulation_mode) {
        if (auto* hrp = std::get_if<visual::HitRoutingPoint>(&hit)) {
            ui::InternedId wire_iid = interner_.intern(hrp->wire_id);
            const bp2::Blueprint::Wire* bp2_wire = host_.find_wire(wire_iid);
            if (bp2_wire && hrp->index < bp2_wire->routing_points.size()) {
                auto new_points = bp2_wire->routing_points;
                new_points.erase(new_points.begin() + static_cast<long>(hrp->index));

                if (!wire_iid.empty()) {
                    snapshot_and_execute(cmd_set_routing_points(wire_iid, std::move(new_points)));
                    rebuild_scene();
                }
            }
            result.double_click_consumed = true;
            return result;
        }
    }

    // Extract the underlying widget from HitNode.
    // Double-click on content still resolves node-level actions
    // (open sub-window, inline value editor).
    std::string_view dbl_click_node_id;
    if (auto* hn = std::get_if<visual::HitNode>(&hit)) {
        dbl_click_node_id = hn->node_id;
    }

    if (!dbl_click_node_id.empty()) {
        std::string node_id(dbl_click_node_id);
        ui::InternedId node_iid = interner_.lookup(dbl_click_node_id);
        const bp2::Blueprint::Node* node = node_iid.empty() ? nullptr : host_.find_node(node_iid);
        if (!read_only && !simulation_mode && node && std::string(interner_.resolve(node->semantic.type)) == "Value") {
            result.open_inline_value_editor = true;
            result.inline_value_editor_node_id = node_id;
            result.has_inline_value_editor_screen_pos = true;
            result.inline_value_editor_screen_pos = screen_pos;
            result.double_click_consumed = true;
            return result;
        }
        if (node && node->is_blueprint_instance()) {
            result.open_sub_window = node_id;
            result.double_click_consumed = true;
            return result;
        }
    }

    if (!read_only && !simulation_mode) {
        if (auto* hw = std::get_if<visual::HitWire>(&hit)) {
            ui::InternedId wire_iid = interner_.intern(hw->wire_id);
            const bp2::Blueprint::Wire* bp2_wire = host_.find_wire(wire_iid);
            if (bp2_wire) {
                auto new_points = bp2_wire->routing_points;
                Pt snapped = editor_math::snap_to_grid(world, viewport_.grid_step);
                size_t insert_idx = hw->segment;
                new_points.insert(new_points.begin() + static_cast<long>(insert_idx), {snapped.x, snapped.y});

                if (!wire_iid.empty()) {
                    snapshot_and_execute(cmd_set_routing_points(wire_iid, std::move(new_points)));
                    rebuild_scene();
                }
            }
            result.double_click_consumed = true;
        }
    }

    // If we reach here, no double-click-specific action was taken.
    // double_click_consumed remains false, signalling the caller to
    // fall through to on_mouse_down so the click still selects/interacts.
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
                    rebuild_scene();
                }
            }
            clear_selection();
            break;

        case Key::Delete:
        case Key::Backspace: {
            if (selected_node_ids_.empty()) break;

            host_.mutate_atomically([&] {
                for (const auto& nid : selected_node_ids_) {
                    if (!nid.empty()) {
                        std::vector<ui::InternedId> connected_wires;
                        connected_wires.reserve(host_.wires().size());
                        for (const auto& w : host_.wires()) {
                            auto [src_node, _src_port] = editor_math::path_to_node_port(w.source, arena_);
                            auto [tgt_node, _tgt_port] = editor_math::path_to_node_port(w.target, arena_);
                            if (src_node == nid || tgt_node == nid) {
                                connected_wires.push_back(w.id);
                            }
                        }
                        host_.remove_node(nid, std::move(connected_wires));
                    }
                }
            });
            debug_validate_command_boundary(host_.current_blueprint(), interner_, arena_, parser_registry_);
            hovered_rp_id_ = {};
            rebuild_scene();
            clear_selection();
            result.rebuild_simulation = true;
            break;
        }

        case Key::RightBracket: {
            viewport_.grid_step_up();
            break;
        }

        case Key::LeftBracket: {
            viewport_.grid_step_down();
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

    constexpr float DEFAULT_W = 64.0f;
    constexpr float DEFAULT_H = 32.0f;
    for (const auto& node : host_.nodes()) {
        float w = node.layout.width.value_or(DEFAULT_W);
        float h = node.layout.height.value_or(DEFAULT_H);
        float cx = node.layout.x + w * 0.5f;
        float cy = node.layout.y + h * 0.5f;
        if (cx >= min_x && cx <= max_x && cy >= min_y && cy <= max_y)
            add_node_selection(node.semantic.id);
    }
}
