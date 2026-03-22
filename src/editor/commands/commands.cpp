#include "commands.h"
#include <spdlog/spdlog.h>
#include <functional>

void execute(bp2::EditorModel& model, ui::StringInterner& interner, Command cmd) {
    std::visit([&](auto c) {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, CmdAddNode>) {
            model.add_node(std::move(c.node));
        } else if constexpr (std::is_same_v<T, CmdRemoveNode>) {
            model.remove_node(c.node_id);
        } else if constexpr (std::is_same_v<T, CmdMoveNode>) {
            model.update_node_position(c.node_id, c.x, c.y);
        } else if constexpr (std::is_same_v<T, CmdAddWire>) {
            model.add_wire(std::move(c.wire));
        } else if constexpr (std::is_same_v<T, CmdRemoveWire>) {
            model.remove_wire(c.wire_id);
        } else if constexpr (std::is_same_v<T, CmdSetParam>) {
            // CmdSetParam has copyable fields, just use c directly
            {
                auto const* n = model.current().find_node(c.node_id);
                if (!n) { spdlog::warn("[cmd] node {} not found", c.node_id.raw()); return; }
                auto updated = *n;
                updated.params[c.key] = c.value;
                model.remove_node(c.node_id);
                model.add_node(std::move(updated));
            }
        } else if constexpr (std::is_same_v<T, CmdResizeNode>) {
            auto const* n = model.current().find_node(c.node_id);
            if (!n) { spdlog::warn("[cmd] node {} not found", c.node_id.raw()); return; }
            auto updated = *n;
            updated.x = c.x; updated.y = c.y; updated.width = c.w; updated.height = c.h;
            model.remove_node(c.node_id);
            model.add_node(std::move(updated));
        } else if constexpr (std::is_same_v<T, CmdSetGridStep>) {
            auto updated = model.current().with_viewport(
                model.current().pan_x(), model.current().pan_y(),
                model.current().zoom(), c.new_step);
            model.replace_current(std::move(updated));
        } else if constexpr (std::is_same_v<T, CmdSetName>) {
            auto const* n = model.current().find_node(c.node_id);
            if (!n) { spdlog::warn("[cmd] node {} not found", c.node_id.raw()); return; }
            auto updated = *n;
            updated.name = std::move(c.new_name);
            model.remove_node(c.node_id);
            model.add_node(std::move(updated));
        } else if constexpr (std::is_same_v<T, CmdAddNested>) {
            model.add_nested(std::move(c.nested));
        } else if constexpr (std::is_same_v<T, CmdRemoveNested>) {
            model.remove_nested(c.nested_id);
        } else if constexpr (std::is_same_v<T, CmdSetRoutingPoints>) {
            auto const* w = model.current().find_wire(c.wire_id);
            if (!w) { spdlog::warn("[cmd] wire {} not found", c.wire_id.raw()); return; }
            auto updated = *w;
            updated.routing_points = std::move(c.points);
            model.remove_wire(c.wire_id);
            model.add_wire(std::move(updated));
        } else if constexpr (std::is_same_v<T, CmdSetPortLayout>) {
            auto const* n = model.current().find_node(c.node_id);
            if (!n) { spdlog::warn("[cmd] node {} not found", c.node_id.raw()); return; }
            auto updated = *n;
            updated.layout_overrides = std::move(c.overrides);
            model.remove_node(c.node_id);
            model.add_node(std::move(updated));
        } else if constexpr (std::is_same_v<T, CmdSetColor>) {
            auto const* n = model.current().find_node(c.node_id);
            if (!n) { spdlog::warn("[cmd] node {} not found", c.node_id.raw()); return; }
            auto updated = *n;
            updated.has_color = c.has_color;
            updated.color_r = c.r;
            updated.color_g = c.g;
            updated.color_b = c.b;
            updated.color_a = c.a;
            model.remove_node(c.node_id);
            model.add_node(std::move(updated));
        }
    }, std::move(cmd));
}
