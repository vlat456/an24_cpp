#include "commands.h"
#include <spdlog/spdlog.h>
#include <functional>

void execute(bp2::EditorModel& model, core::StringInterner& interner, Command cmd) {
    std::visit([&](auto c) {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, CmdAddNode>) {
            model.add_node(std::move(c.node));
        } else if constexpr (std::is_same_v<T, CmdRemoveNode>) {
            for (core::InternedId const wid : c.connected_wire_ids) {
                model.remove_wire(wid);
            }
            model.remove_node(c.node_id);
        } else if constexpr (std::is_same_v<T, CmdMoveNode>) {
            model.update_node_position(c.node_id, c.x, c.y);
        } else if constexpr (std::is_same_v<T, CmdAddWire>) {
            model.add_wire(std::move(c.wire));
        } else if constexpr (std::is_same_v<T, CmdRemoveWire>) {
            model.remove_wire(c.wire_id);
         } else if constexpr (std::is_same_v<T, CmdSetParam>) {
             if (!model.update_node(c.node_id, [&](bp2::Blueprint::Node& n) {
                 n.semantic.params[c.key] = c.value;
            })) {
                spdlog::warn("[cmd] node {} not found", c.node_id.raw());
            }
         } else if constexpr (std::is_same_v<T, CmdResizeNode>) {
             if (!model.update_node(c.node_id, [&](bp2::Blueprint::Node& n) {
                 n.layout.x = c.x; n.layout.y = c.y; n.layout.width = c.w; n.layout.height = c.h;
            })) {
                spdlog::warn("[cmd] node {} not found", c.node_id.raw());
            }
         } else if constexpr (std::is_same_v<T, CmdSetName>) {
             if (!model.update_node(c.node_id, [&](bp2::Blueprint::Node& n) {
                 n.view.name = std::move(c.new_name);
             })) {
                 spdlog::warn("[cmd] node {} not found", c.node_id.raw());
             }
        } else if constexpr (std::is_same_v<T, CmdSetRoutingPoints>) {
            if (!model.update_wire(c.wire_id, [&](bp2::Blueprint::Wire& w) {
                w.routing_points = std::move(c.points);
            })) {
                spdlog::warn("[cmd] wire {} not found", c.wire_id.raw());
            }
         } else if constexpr (std::is_same_v<T, CmdSetPortLayout>) {
             if (!model.update_node(c.node_id, [&](bp2::Blueprint::Node& n) {
                 n.layout.layout_overrides = std::move(c.overrides);
             })) {
                 spdlog::warn("[cmd] node {} not found", c.node_id.raw());
             }
         }
    }, std::move(cmd));
}
