#include "owner_scope.h"

namespace bp2 {

std::optional<std::string> validate_owner_scope_reference(const Blueprint& bp,
                                                          const Blueprint::Node& node,
                                                          ui::StringInterner& interner) {
    if (node.structure.owner_scope.empty()) {
        return std::nullopt;
    }

    const ui::InternedId owner_id = interner.lookup(node.structure.owner_scope);
    if (owner_id.empty()) {
        return "owner_scope references unknown node id='" + node.structure.owner_scope + "'";
    }

    const auto* host = bp.find_node(owner_id);
    if (!host) {
        return "owner_scope references missing node id='" + node.structure.owner_scope + "'";
    }
    if (!host->view.expandable) {
        return "owner_scope references non-expandable node id='" + node.structure.owner_scope + "'";
    }

    const auto* nested = bp.find_hosted_nested(*host);
    if (!nested) {
        return "owner_scope references node without hosted nested id='" + node.structure.owner_scope + "'";
    }
    if (!nested->is_embedded()) {
        return "owner_scope references non-embedded nested host id='" + node.structure.owner_scope + "'";
    }

    return std::nullopt;
}

} // namespace bp2
