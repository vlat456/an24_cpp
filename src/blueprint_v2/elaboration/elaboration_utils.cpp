#include "elaboration_utils.h"

#include "core/solvers/common/signal_key.h"

namespace bp2::elaboration {

std::string node_id_from_path(Path node_path, PathArena& arena, const ui::StringInterner& interner) {
    std::vector<std::string> segments;
    Path cur = node_path;
    while (cur.kind() != PathKind::Root) {
        if (cur.kind() == PathKind::Nested || cur.kind() == PathKind::Node) {
            segments.emplace_back(interner.resolve(cur.segment()));
        }
        cur = arena.parent(cur);
    }

    std::string out;
    for (auto it = segments.rbegin(); it != segments.rend(); ++it) {
        if (!out.empty()) out.push_back(':');
        out += *it;
    }
    return out;
}

std::string exposed_key_for_bridge(
    std::string_view bridge_dev_id,
    const ui::InternedId& exposed_port_name,
    const ui::StringInterner& interner)
{
    const size_t sep = bridge_dev_id.rfind(':');
    if (sep == std::string_view::npos || sep == 0 || (sep + 1) >= bridge_dev_id.size()) {
        return "";
    }
    const std::string_view parent_instance = bridge_dev_id.substr(0, sep);
    return signal_key::make_node_port_key(parent_instance, interner.resolve(exposed_port_name));
}

} // namespace bp2::elaboration
