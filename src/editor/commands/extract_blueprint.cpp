#include "extract_blueprint.h"

#include "editor/common/port_type_utils.h"
#include "editor/visual/persist.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace editor::commands {

namespace {

// Layout constants for bridge node positioning
static constexpr float kBridgeMarginX     = 160.0f;
static constexpr float kDefaultNodeWidth  = 100.0f;
static constexpr float kDefaultNodeHeight = 64.0f;
static constexpr float kFallbackLaneStartY = 40.0f;
static constexpr float kFallbackLaneStepY  = 80.0f;
static constexpr float kMultiLaneOffsetY   = 16.0f;

struct ExternalConnection {
    bool is_input = false;
    ui::InternedId external_node_id;
    ui::InternedId external_port;
    ui::InternedId internal_node_id;
    ui::InternedId internal_port;
    std::string iface_name;
    Domain domain = Domain::Electrical;
    PortType port_type = PortType::Any;
    ui::InternedId original_wire_id;
};

struct ExtractionPlan {
    std::vector<bp2::Blueprint::Node> internal_nodes;
    std::vector<bp2::Blueprint::Wire> internal_wires;
    std::vector<ExternalConnection> inputs;
    std::vector<ExternalConnection> outputs;
    std::unordered_set<ui::InternedId> selected_set;
    float min_x = 0.0f;
    float min_y = 0.0f;
    float max_x = 0.0f;
    float max_y = 0.0f;
    float center_x = 0.0f;
    float center_y = 0.0f;
};

struct DescendantRemapStats {
    size_t remapped = 0;
    size_t passthrough = 0;
};

static bool validate_selected_embedded_nested_merge_safety(const bp2::Blueprint& source,
                                                           const std::unordered_set<ui::InternedId>& selected_set,
                                                           bool allow_nonembedded_descendant_refs,
                                                           std::string* error_out);

static DescendantRemapStats collect_descendant_remap_stats(
    const bp2::Blueprint& source,
    const std::unordered_set<ui::InternedId>& selected_set,
    bool allow_nonembedded_descendant_refs);

static PortType find_port_type(const bp2::Blueprint::Node* node, ui::InternedId port_name) {
    if (!node) return PortType::Any;
    for (const auto& p : node->inputs) {
        if (p.name == port_name) return p.type;
    }
    for (const auto& p : node->outputs) {
        if (p.name == port_name) return p.type;
    }
    return PortType::Any;
}

static bool path_to_node_port(const bp2::Path& path,
                              const bp2::PathArena& arena,
                              ui::InternedId& out_node,
                              ui::InternedId& out_port) {
    if (path.kind() != bp2::PathKind::Port) return false;
    const bp2::Path parent = arena.parent(path);
    if (parent.kind() != bp2::PathKind::Node) return false;
    out_node = parent.segment();
    out_port = path.segment();
    return !out_node.empty() && !out_port.empty();
}

static std::string dedupe_name(const std::string& base,
                               std::unordered_set<std::string>& used) {
    if (used.find(base) == used.end()) {
        used.insert(base);
        return base;
    }
    for (int i = 2; i < 1000000; ++i) {
        std::string candidate = base + "_" + std::to_string(i);
        if (used.find(candidate) == used.end()) {
            used.insert(candidate);
            return candidate;
        }
    }
    return base + "_overflow";
}

static ui::InternedId next_unique_id(ui::StringInterner& interner,
                                     const std::unordered_set<ui::InternedId>& used,
                                     const std::string& prefix) {
    for (int i = 1; i < 1000000; ++i) {
        std::string candidate = prefix + std::to_string(i);
        ui::InternedId id = interner.lookup(candidate);
        if (id.empty() || used.find(id) == used.end()) {
            return interner.intern(candidate);
        }
    }
    return {};
}

static std::unordered_set<ui::InternedId> collect_used_node_ids(const bp2::Blueprint& bp) {
    std::unordered_set<ui::InternedId> out;
    out.reserve(bp.nodes().size() + bp.nested().size());
    for (const auto& n : bp.nodes()) out.insert(n.id);
    for (const auto& n : bp.nested()) out.insert(n.id);
    return out;
}

static std::unordered_set<ui::InternedId> collect_used_wire_ids(const bp2::Blueprint& bp) {
    std::unordered_set<ui::InternedId> out;
    out.reserve(bp.wires().size());
    for (const auto& w : bp.wires()) out.insert(w.id);
    return out;
}

static bool compare_external(const ExternalConnection& a, const ExternalConnection& b) {
    return std::make_tuple(a.original_wire_id.raw(),
                           a.external_node_id.raw(), a.external_port.raw(),
                           a.internal_node_id.raw(), a.internal_port.raw())
        < std::make_tuple(b.original_wire_id.raw(),
                          b.external_node_id.raw(), b.external_port.raw(),
                          b.internal_node_id.raw(), b.internal_port.raw());
}

static std::unordered_map<ui::InternedId, float> build_node_center_y_map(
    const std::vector<bp2::Blueprint::Node>& nodes) {
    std::unordered_map<ui::InternedId, float> out;
    out.reserve(nodes.size());
    for (const auto& n : nodes) {
        const float h = n.height.value_or(kDefaultNodeHeight);
        out[n.id] = n.y + (h * 0.5f);
    }
    return out;
}

static float fallback_lane_y(size_t index) {
    return kFallbackLaneStartY + static_cast<float>(index) * kFallbackLaneStepY;
}

static bp2::Blueprint::Nested clone_nested(const bp2::Blueprint::Nested& n) {
    return bp2::Blueprint::Nested(n);
}

static ui::InternedId make_iface_bridge_id(ui::StringInterner& interner,
                                           ui::InternedId nested_instance_id,
                                           const std::string& iface_name) {
    std::string id = std::string(interner.resolve(nested_instance_id));
    id += ":";
    id += iface_name;
    return interner.intern(id);
}

static bool validate_blueprint_name_for_extract(const bp2::Blueprint& source,
                                                const std::string& blueprint_name,
                                                ui::StringInterner& interner,
                                                ui::InternedId* blueprint_iid_out,
                                                std::string* error_out) {
    if (blueprint_name.empty()) {
        if (error_out) *error_out = "extract blueprint name must be non-empty";
        return false;
    }

    const ui::InternedId blueprint_iid = blueprint_iid_out
        ? interner.intern(blueprint_name)
        : interner.lookup(blueprint_name);
    for (const auto& n : source.nested()) {
        // On preview path (lookup), blueprint_iid may be empty if the name was
        // never interned. Fall back to string comparison to avoid false negatives.
        const bool matches = !blueprint_iid.empty()
            ? (n.blueprint_id == blueprint_iid)
            : (interner.resolve(n.blueprint_id) == blueprint_name);
        if (matches) {
            if (error_out) *error_out = "blueprint name already exists in nested definitions";
            return false;
        }
    }
    for (const auto& n : source.nodes()) {
        if (n.name == blueprint_name) {
            if (error_out) *error_out = "blueprint name already exists as node name";
            return false;
        }
    }

    if (blueprint_iid_out) *blueprint_iid_out = blueprint_iid;
    return true;
}

struct BridgeSideBuildParams {
    const std::vector<ExternalConnection>& conns;
    bool is_input_side = true;
    const std::unordered_map<ui::InternedId, float>& node_center_y;
    float x = 0.0f;
    float fallback_y_origin = 0.0f;
    std::string group_id;
    const char* unique_prefix = "";
    const ui::InternedId* canonical_nested_instance_id = nullptr;
};

static bool create_bridge_nodes_for_side(
    bp2::Blueprint& out,
    const BridgeSideBuildParams& p,
    ui::StringInterner& interner,
    std::unordered_set<ui::InternedId>& used_node_ids,
    std::unordered_map<std::string, ui::InternedId>& out_bridge_ids,
    std::string* error_out) {
    std::vector<size_t> order(p.conns.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        float ay = p.fallback_y_origin + fallback_lane_y(a);
        float by = p.fallback_y_origin + fallback_lane_y(b);
        if (auto it = p.node_center_y.find(p.conns[a].internal_node_id); it != p.node_center_y.end()) ay = it->second;
        if (auto it = p.node_center_y.find(p.conns[b].internal_node_id); it != p.node_center_y.end()) by = it->second;
        const float epsilon = 0.5f;
        if (ay < by - epsilon) return true;
        if (ay > by + epsilon) return false;
        return p.conns[a].iface_name < p.conns[b].iface_name;
    });

    std::unordered_map<ui::InternedId, int> lane_counts;
    for (size_t rank = 0; rank < order.size(); ++rank) {
        const auto& ec = p.conns[order[rank]];

        ui::InternedId id;
        if (p.canonical_nested_instance_id) {
            id = make_iface_bridge_id(interner, *p.canonical_nested_instance_id, ec.iface_name);
            if (used_node_ids.find(id) != used_node_ids.end()) {
                if (error_out) *error_out = "extract bridge node id collision";
                return false;
            }
        } else {
            id = next_unique_id(interner, used_node_ids, p.unique_prefix);
        }
        used_node_ids.insert(id);
        out_bridge_ids[ec.iface_name] = id;

        bp2::Blueprint::Node n;
        n.id = id;
        n.type = interner.intern(p.is_input_side ? "BlueprintInput" : "BlueprintOutput");
        n.name = ec.iface_name;
        n.group_id = p.group_id;
        n.x = p.x;

        float base_y = p.fallback_y_origin + fallback_lane_y(rank);
        if (auto it = p.node_center_y.find(ec.internal_node_id); it != p.node_center_y.end()) {
            const int lane = lane_counts[ec.internal_node_id]++;
            base_y = it->second + static_cast<float>(lane) * kMultiLaneOffsetY;
        }
        n.y = base_y;

        const PortType pt = (ec.port_type == PortType::Any)
            ? editor::common::port_type_for_domain(ec.domain)
            : ec.port_type;
        const Domain pd = editor::common::domain_for_port_type(pt);
        if (p.is_input_side) {
            n.inputs.emplace_back(interner.intern("ext"), PortSide::Input, pt);
            n.outputs.emplace_back(interner.intern("port"), PortSide::Output, pt);
            n.iface = bp2::Interface({
                {interner.intern("ext"), pd, bp2::Direction::Input},
                {interner.intern("port"), pd, bp2::Direction::Output},
            });
        } else {
            n.inputs.emplace_back(interner.intern("port"), PortSide::Input, pt);
            n.outputs.emplace_back(interner.intern("ext"), PortSide::Output, pt);
            n.iface = bp2::Interface({
                {interner.intern("ext"), pd, bp2::Direction::Output},
                {interner.intern("port"), pd, bp2::Direction::Input},
            });
        }
        out = out.with_node(std::move(n));
    }
    return true;
}

static void append_bridge_to_internal_wires(bp2::Blueprint& out,
                                            const std::vector<ExternalConnection>& conns,
                                            bool is_input_side,
                                            const std::unordered_map<std::string, ui::InternedId>& bridge_ids,
                                            const char* wire_prefix,
                                            ui::StringInterner& interner,
                                            bp2::PathArena& arena,
                                            std::unordered_set<ui::InternedId>& used_wire_ids) {
    for (const auto& ec : conns) {
        bp2::Blueprint::Wire w;
        w.id = next_unique_id(interner, used_wire_ids, wire_prefix);
        used_wire_ids.insert(w.id);
        w.domain = ec.domain;
        if (is_input_side) {
            w.source = arena.make_port(arena.make_node(arena.root(), bridge_ids.at(ec.iface_name)), interner.intern("port"));
            w.target = arena.make_port(arena.make_node(arena.root(), ec.internal_node_id), ec.internal_port);
        } else {
            w.source = arena.make_port(arena.make_node(arena.root(), ec.internal_node_id), ec.internal_port);
            w.target = arena.make_port(arena.make_node(arena.root(), bridge_ids.at(ec.iface_name)), interner.intern("port"));
        }
        out = out.with_wire(std::move(w));
    }
}

static std::optional<ExtractionPlan> analyze_selection(const bp2::Blueprint& bp,
                                                        const std::vector<ui::InternedId>& selected_ids,
                                                        const std::string& group_id,
                                                        bool allow_nonembedded_descendant_refs,
                                                        ui::StringInterner& interner,
                                                        const bp2::PathArena& arena,
                                                        std::string* error_out) {
    ExtractionPlan plan;
    plan.selected_set.insert(selected_ids.begin(), selected_ids.end());
    if (plan.selected_set.size() < 2) {
        if (error_out) *error_out = "extract requires at least 2 selected nodes";
        return std::nullopt;
    }

    float min_x = std::numeric_limits<float>::max();
    float min_y = std::numeric_limits<float>::max();
    float max_x = -std::numeric_limits<float>::max();
    float max_y = -std::numeric_limits<float>::max();

    for (const auto& node : bp.nodes()) {
        if (plan.selected_set.find(node.id) == plan.selected_set.end()) continue;
        if (node.group_id != group_id) {
            if (error_out) *error_out = "selected nodes must belong to active group";
            return std::nullopt;
        }
        if (node.type == interner.intern("BlueprintInput")
            || node.type == interner.intern("BlueprintOutput")) {
            if (error_out) *error_out = "extract does not support selecting BlueprintInput/BlueprintOutput bridge nodes";
            return std::nullopt;
        }
        const bp2::Blueprint::Nested* nested = bp.find_nested(node.id);
        if (nested) {
            if (!nested->embedded) {
                if (error_out) *error_out = "extract does not support selecting non-embedded nested instances";
                return std::nullopt;
            }
        } else if (node.expandable) {
            if (error_out) *error_out = "extract nested instance metadata missing";
            return std::nullopt;
        }
        plan.internal_nodes.push_back(node);
        min_x = std::min(min_x, node.x);
        min_y = std::min(min_y, node.y);
        max_x = std::max(max_x, node.x + node.width.value_or(kDefaultNodeWidth));
        max_y = std::max(max_y, node.y + node.height.value_or(kDefaultNodeHeight));
    }

    if (plan.internal_nodes.size() < 2) {
        if (error_out) *error_out = "selected nodes not found in blueprint";
        return std::nullopt;
    }

    plan.min_x = min_x;
    plan.min_y = min_y;
    plan.max_x = max_x;
    plan.max_y = max_y;
    plan.center_x = (min_x + max_x) * 0.5f;
    plan.center_y = (min_y + max_y) * 0.5f;

    for (const auto& wire : bp.wires()) {
        ui::InternedId src_node, src_port, tgt_node, tgt_port;
        if (!path_to_node_port(wire.source, arena, src_node, src_port)
            || !path_to_node_port(wire.target, arena, tgt_node, tgt_port)) {
            if (error_out) *error_out = "wire endpoint path unresolved during extraction";
            return std::nullopt;
        }

        const bool src_selected = plan.selected_set.find(src_node) != plan.selected_set.end();
        const bool tgt_selected = plan.selected_set.find(tgt_node) != plan.selected_set.end();

        if (src_selected && tgt_selected) {
            plan.internal_wires.push_back(wire);
            continue;
        }
        if (!src_selected && !tgt_selected) {
            continue;
        }

        const bp2::Blueprint::Node* src_node_ptr = bp.find_node(src_node);
        const bp2::Blueprint::Node* tgt_node_ptr = bp.find_node(tgt_node);

        ExternalConnection ec;
        ec.original_wire_id = wire.id;
        ec.domain = wire.domain;
        if (!src_selected && tgt_selected) {
            ec.is_input = true;
            ec.external_node_id = src_node;
            ec.external_port = src_port;
            ec.internal_node_id = tgt_node;
            ec.internal_port = tgt_port;
            ec.iface_name = std::string(interner.resolve(tgt_port));
            ec.port_type = find_port_type(tgt_node_ptr, tgt_port);
            plan.inputs.push_back(std::move(ec));
        } else {
            ec.is_input = false;
            ec.external_node_id = tgt_node;
            ec.external_port = tgt_port;
            ec.internal_node_id = src_node;
            ec.internal_port = src_port;
            ec.iface_name = std::string(interner.resolve(src_port));
            ec.port_type = find_port_type(src_node_ptr, src_port);
            plan.outputs.push_back(std::move(ec));
        }
    }

    std::sort(plan.inputs.begin(), plan.inputs.end(), compare_external);
    std::sort(plan.outputs.begin(), plan.outputs.end(), compare_external);

    std::unordered_set<std::string> input_name_used;
    for (auto& ec : plan.inputs) {
        if (ec.iface_name.empty()) ec.iface_name = "in";
        ec.iface_name = dedupe_name(ec.iface_name, input_name_used);
    }
    std::unordered_set<std::string> output_name_used;
    for (auto& ec : plan.outputs) {
        if (ec.iface_name.empty()) ec.iface_name = "out";
        ec.iface_name = dedupe_name(ec.iface_name, output_name_used);
    }

    if (!validate_selected_embedded_nested_merge_safety(
            bp,
            plan.selected_set,
            allow_nonembedded_descendant_refs,
            error_out)) {
        return std::nullopt;
    }

    return plan;
}

static bool append_selected_embedded_nested_for_inline(
    bp2::Blueprint& inline_bp,
    const bp2::Blueprint& source,
    const std::unordered_set<ui::InternedId>& selected_set,
    bool allow_nonembedded_descendant_refs,
    std::string* error_out) {
    // Index embedded nested definitions by blueprint_id so guarded mode can
    // remap non-embedded descendants into embedded inline definitions.
    std::unordered_map<ui::InternedId, const bp2::Blueprint::Nested*> embedded_by_blueprint_id;
    for (const auto& n : source.nested()) {
        if (!n.embedded || !n.inline_def) continue;
        auto it = embedded_by_blueprint_id.find(n.blueprint_id);
        if (it == embedded_by_blueprint_id.end() || n.id.raw() < it->second->id.raw()) {
            embedded_by_blueprint_id[n.blueprint_id] = &n;
        }
    }

    std::function<bool(bp2::Blueprint::Nested&)> remap_descendants;
    remap_descendants = [&](bp2::Blueprint::Nested& owner) -> bool {
        if (!owner.inline_def) return true;

        std::vector<bp2::Blueprint::Nested> remapped;
        remapped.reserve(owner.inline_def->nested().size());
        for (const auto& child_src : owner.inline_def->nested()) {
            bp2::Blueprint::Nested child = child_src;
            if (!child.embedded) {
                auto it = embedded_by_blueprint_id.find(child.blueprint_id);
                if (it != embedded_by_blueprint_id.end()) {
                    const bp2::Blueprint::Nested* resolved = it->second;
                    child.embedded = true;
                    child.iface = resolved->iface;
                    if (resolved->inline_def) {
                        child.inline_def = std::make_unique<bp2::Blueprint>(*resolved->inline_def);
                    }
                } else if (!allow_nonembedded_descendant_refs) {
                    if (error_out) {
                        *error_out = "selected embedded nested contains non-embedded descendant references";
                    }
                    return false;
                }
            }

            if (!remap_descendants(child)) return false;
            remapped.push_back(std::move(child));
        }

        bp2::Blueprint rebuilt = *owner.inline_def;
        for (const auto& existing : owner.inline_def->nested()) {
            rebuilt = rebuilt.without_nested(existing.id);
        }
        for (auto& child : remapped) {
            rebuilt = rebuilt.with_nested(std::move(child));
        }
        owner.inline_def = std::make_unique<bp2::Blueprint>(std::move(rebuilt));
        return true;
    };

    std::vector<const bp2::Blueprint::Nested*> selected_nested;
    selected_nested.reserve(source.nested().size());
    for (const auto& n : source.nested()) {
        if (selected_set.find(n.id) == selected_set.end()) continue;
        selected_nested.push_back(&n);
    }

    std::sort(selected_nested.begin(), selected_nested.end(), [](const auto* a, const auto* b) {
        return a->id.raw() < b->id.raw();
    });

    for (const auto* n : selected_nested) {
        if (!n->embedded) {
            if (error_out) *error_out = "selected non-embedded nested instance cannot be inlined";
            return false;
        }
        if (!n->inline_def) {
            if (error_out) *error_out = "selected embedded nested instance missing inline_def";
            return false;
        }
        if (inline_bp.find_nested(n->id) != nullptr) {
            if (error_out) *error_out = "inline merge nested id collision";
            return false;
        }
        bp2::Blueprint::Nested copy = clone_nested(*n);
        if (!remap_descendants(copy)) return false;
        inline_bp = inline_bp.with_nested(std::move(copy));
    }
    return true;
}

static bool contains_nonembedded_descendant_nested(const bp2::Blueprint& bp) {
    for (const auto& n : bp.nested()) {
        if (!n.embedded) return true;
        if (n.inline_def && contains_nonembedded_descendant_nested(*n.inline_def)) return true;
    }
    return false;
}

static bool validate_selected_embedded_nested_merge_safety(const bp2::Blueprint& source,
                                                           const std::unordered_set<ui::InternedId>& selected_set,
                                                           bool allow_nonembedded_descendant_refs,
                                                           std::string* error_out) {
    if (allow_nonembedded_descendant_refs) return true;
    for (const auto& n : source.nested()) {
        if (selected_set.find(n.id) == selected_set.end()) continue;
        if (!n.embedded || !n.inline_def) continue;
        if (contains_nonembedded_descendant_nested(*n.inline_def)) {
            if (error_out) {
                *error_out = "selected embedded nested contains non-embedded descendant references";
            }
            return false;
        }
    }
    return true;
}

static DescendantRemapStats collect_descendant_remap_stats(
    const bp2::Blueprint& source,
    const std::unordered_set<ui::InternedId>& selected_set,
    bool allow_nonembedded_descendant_refs) {
    DescendantRemapStats stats;

    std::unordered_map<ui::InternedId, const bp2::Blueprint::Nested*> embedded_by_blueprint_id;
    for (const auto& n : source.nested()) {
        if (!n.embedded || !n.inline_def) continue;
        auto it = embedded_by_blueprint_id.find(n.blueprint_id);
        if (it == embedded_by_blueprint_id.end() || n.id.raw() < it->second->id.raw()) {
            embedded_by_blueprint_id[n.blueprint_id] = &n;
        }
    }

    std::function<void(const bp2::Blueprint::Nested&)> visit_nested;
    visit_nested = [&](const bp2::Blueprint::Nested& owner) {
        if (!owner.inline_def) return;
        for (const auto& child : owner.inline_def->nested()) {
            if (!child.embedded) {
                if (embedded_by_blueprint_id.find(child.blueprint_id) != embedded_by_blueprint_id.end()) {
                    ++stats.remapped;
                } else if (allow_nonembedded_descendant_refs) {
                    ++stats.passthrough;
                }
            }
            visit_nested(child);
        }
    };

    for (const auto& n : source.nested()) {
        if (selected_set.find(n.id) == selected_set.end()) continue;
        if (!n.embedded || !n.inline_def) continue;
        visit_nested(n);
    }

    return stats;
}

static std::optional<bp2::Blueprint> build_inline_blueprint(const ExtractionPlan& plan,
                                                             const bp2::Blueprint& source,
                                                             bool allow_nonembedded_descendant_refs,
                                                             ui::StringInterner& interner,
                                                             bp2::PathArena& arena,
                                                             ui::InternedId blueprint_id,
                                                             std::string* error_out) {
    bp2::Blueprint out;
    out = out.with_id(blueprint_id);
    out = out.with_display_name(std::string(interner.resolve(blueprint_id)));
    out = out.with_name(std::string(interner.resolve(blueprint_id)));

    const float min_x = plan.min_x;
    const float min_y = plan.min_y;
    const float left_margin = kBridgeMarginX;

    std::vector<bp2::Blueprint::Node> translated_nodes;
    translated_nodes.reserve(plan.internal_nodes.size());
    float max_internal_right = 0.0f;
    for (auto node : plan.internal_nodes) {
        node.x = (node.x - min_x) + left_margin;
        node.y = (node.y - min_y);
        node.group_id.clear();
        max_internal_right = std::max(max_internal_right, node.x + node.width.value_or(kDefaultNodeWidth));
        translated_nodes.push_back(node);
        out = out.with_node(std::move(node));
    }

    std::vector<bp2::PortDescriptor> iface_ports;
    iface_ports.reserve(plan.inputs.size() + plan.outputs.size());
    for (const auto& ec : plan.inputs) {
        iface_ports.push_back({interner.intern(ec.iface_name), ec.domain, bp2::Direction::Input});
    }
    for (const auto& ec : plan.outputs) {
        iface_ports.push_back({interner.intern(ec.iface_name), ec.domain, bp2::Direction::Output});
    }
    out = out.with_interface(bp2::Interface(std::move(iface_ports)));

    std::unordered_set<ui::InternedId> used_node_ids = collect_used_node_ids(out);
    std::unordered_set<ui::InternedId> used_wire_ids = collect_used_wire_ids(out);

    std::unordered_map<std::string, ui::InternedId> input_bridge_ids;
    std::unordered_map<std::string, ui::InternedId> output_bridge_ids;

    // Use translated (local-coordinate) nodes so bridge Y aligns with actual positions
    const auto node_center_y = build_node_center_y_map(translated_nodes);
    BridgeSideBuildParams inline_input_params{plan.inputs, true, node_center_y, 0.0f, 0.0f, "", "bp_in_", nullptr};
    if (!create_bridge_nodes_for_side(out,
                                      inline_input_params,
                                      interner,
                                      used_node_ids,
                                      input_bridge_ids,
                                      error_out)) {
        return std::nullopt;
    }
    BridgeSideBuildParams inline_output_params{plan.outputs,
                                               false,
                                               node_center_y,
                                                max_internal_right + kBridgeMarginX,
                                               0.0f,
                                               "",
                                               "bp_out_",
                                               nullptr};
    if (!create_bridge_nodes_for_side(out,
                                      inline_output_params,
                                      interner,
                                      used_node_ids,
                                      output_bridge_ids,
                                      error_out)) {
        return std::nullopt;
    }

    for (const auto& w : plan.internal_wires) {
        ui::InternedId src_n, src_p, tgt_n, tgt_p;
        path_to_node_port(w.source, arena, src_n, src_p);
        path_to_node_port(w.target, arena, tgt_n, tgt_p);

        bp2::Blueprint::Wire nw = w;
        nw.source = arena.make_port(arena.make_node(arena.root(), src_n), src_p);
        nw.target = arena.make_port(arena.make_node(arena.root(), tgt_n), tgt_p);
        out = out.with_wire(std::move(nw));
    }

    append_bridge_to_internal_wires(out,
                                    plan.inputs,
                                    true,
                                    input_bridge_ids,
                                    "bp_bridge_in_wire_",
                                    interner,
                                    arena,
                                    used_wire_ids);
    append_bridge_to_internal_wires(out,
                                    plan.outputs,
                                    false,
                                    output_bridge_ids,
                                    "bp_bridge_out_wire_",
                                    interner,
                                    arena,
                                    used_wire_ids);

    if (!append_selected_embedded_nested_for_inline(
            out,
            source,
            plan.selected_set,
            allow_nonembedded_descendant_refs,
            error_out)) {
        return std::nullopt;
    }

    return out;
}

static std::optional<bp2::Blueprint> build_parent_blueprint_from_plan(
    const bp2::Blueprint& source,
    const ExtractionPlan& plan,
    ui::InternedId blueprint_iid,
    const std::string& blueprint_name,
    const std::string& group_id,
    bool allow_nonembedded_descendant_refs,
    ui::StringInterner& interner,
    bp2::PathArena& arena,
    std::string* error_out) {
    bp2::Blueprint out;
    out = out.with_id(source.id());
    out = out.with_display_name(source.display_name());
    out = out.with_name(source.name());
    out = out.with_interface(source.iface());
    out = out.with_viewport(source.pan_x(), source.pan_y(), source.zoom(), source.grid_step());

    std::unordered_set<ui::InternedId> used_node_ids = collect_used_node_ids(source);
    std::unordered_set<ui::InternedId> used_wire_ids = collect_used_wire_ids(source);
    ui::InternedId nested_instance_id = next_unique_id(interner, used_node_ids, "extract_inst_");
    if (nested_instance_id.empty()) {
        if (error_out) *error_out = "failed to allocate nested instance id";
        return std::nullopt;
    }
    used_node_ids.insert(nested_instance_id);
    const std::string nested_group_id = std::string(interner.resolve(nested_instance_id));

    for (const auto& nsrc : source.nodes()) {
        auto n = nsrc;
        if (plan.selected_set.find(n.id) != plan.selected_set.end()) {
            n.group_id = nested_group_id;
        }
        out = out.with_node(std::move(n));
    }

    for (const auto& w : source.wires()) {
        ui::InternedId src_node, src_port, tgt_node, tgt_port;
        if (!path_to_node_port(w.source, arena, src_node, src_port)
            || !path_to_node_port(w.target, arena, tgt_node, tgt_port)) {
            if (error_out) *error_out = "wire endpoint path unresolved during extraction";
            return std::nullopt;
        }
        const bool src_selected = plan.selected_set.find(src_node) != plan.selected_set.end();
        const bool tgt_selected = plan.selected_set.find(tgt_node) != plan.selected_set.end();
        if (src_selected != tgt_selected) continue;
        auto nw = w;
        nw.source = arena.make_port(arena.make_node(arena.root(), src_node), src_port);
        nw.target = arena.make_port(arena.make_node(arena.root(), tgt_node), tgt_port);
        out = out.with_wire(std::move(nw));
    }

    for (const auto& n : source.nested()) {
        out = out.with_nested(clone_nested(n));
    }

    auto inline_bp_opt = build_inline_blueprint(
        plan,
        source,
        allow_nonembedded_descendant_refs,
        interner,
        arena,
        blueprint_iid,
        error_out);
    if (!inline_bp_opt) return std::nullopt;
    bp2::Blueprint inline_bp = std::move(*inline_bp_opt);

    bp2::Blueprint::Nested nested;
    nested.id = nested_instance_id;
    nested.blueprint_id = blueprint_iid;
    nested.embedded = true;
    nested.inline_def = std::make_unique<bp2::Blueprint>(std::move(inline_bp));
    nested.iface = nested.inline_def->iface();
    nested.x = plan.center_x;
    nested.y = plan.center_y;
    out = out.with_nested(std::move(nested));

    bp2::Blueprint::Node collapsed;
    collapsed.id = nested_instance_id;
    collapsed.type = blueprint_iid;
    collapsed.name = blueprint_name;
    collapsed.expandable = true;
    collapsed.collapsed = true;
    collapsed.blueprint_path = blueprint_name;
    collapsed.group_id = group_id;
    collapsed.x = plan.center_x;
    collapsed.y = plan.center_y;
    collapsed.width = 160.0f;
    collapsed.height = 64.0f;
    std::vector<ExternalConnection> sorted_inputs = plan.inputs;
    std::vector<ExternalConnection> sorted_outputs = plan.outputs;
    std::sort(sorted_inputs.begin(), sorted_inputs.end(), compare_external);
    std::sort(sorted_outputs.begin(), sorted_outputs.end(), compare_external);
    for (const auto& ec : sorted_inputs) {
        const PortType pt = (ec.port_type == PortType::Any)
            ? editor::common::port_type_for_domain(ec.domain)
            : ec.port_type;
        collapsed.inputs.emplace_back(interner.intern(ec.iface_name), PortSide::Input, pt);
    }
    for (const auto& ec : sorted_outputs) {
        const PortType pt = (ec.port_type == PortType::Any)
            ? editor::common::port_type_for_domain(ec.domain)
            : ec.port_type;
        collapsed.outputs.emplace_back(interner.intern(ec.iface_name), PortSide::Output, pt);
    }
    out = out.with_node(std::move(collapsed));

    std::unordered_map<std::string, ui::InternedId> input_bridge_ids;
    std::unordered_map<std::string, ui::InternedId> output_bridge_ids;

    std::unordered_set<std::string> input_iface_names;
    input_iface_names.reserve(plan.inputs.size());
    for (const auto& ec_in : plan.inputs) {
        input_iface_names.insert(ec_in.iface_name);
    }
    for (const auto& ec_out : plan.outputs) {
        if (input_iface_names.find(ec_out.iface_name) != input_iface_names.end()) {
            if (error_out) *error_out = "extract iface name collision between input/output";
            return std::nullopt;
        }
    }

    const auto node_center_y = build_node_center_y_map(plan.internal_nodes);

    BridgeSideBuildParams parent_input_params{plan.inputs,
                                               true,
                                               node_center_y,
                                               plan.min_x - 160.0f,
                                               plan.min_y,
                                               nested_group_id,
                                               "",
                                               &nested_instance_id};
    if (!create_bridge_nodes_for_side(out,
                                      parent_input_params,
                                      interner,
                                      used_node_ids,
                                      input_bridge_ids,
                                      error_out)) {
        return std::nullopt;
    }
    BridgeSideBuildParams parent_output_params{plan.outputs,
                                                false,
                                                node_center_y,
                                                plan.max_x + 160.0f,
                                                plan.min_y,
                                                nested_group_id,
                                                "",
                                                &nested_instance_id};
    if (!create_bridge_nodes_for_side(out,
                                      parent_output_params,
                                      interner,
                                      used_node_ids,
                                      output_bridge_ids,
                                      error_out)) {
        return std::nullopt;
    }

    append_bridge_to_internal_wires(out,
                                    plan.inputs,
                                    true,
                                    input_bridge_ids,
                                    "extract_wire_",
                                    interner,
                                    arena,
                                    used_wire_ids);
    append_bridge_to_internal_wires(out,
                                    plan.outputs,
                                    false,
                                    output_bridge_ids,
                                    "extract_wire_",
                                    interner,
                                    arena,
                                    used_wire_ids);

    for (const auto& ec : plan.inputs) {
        bp2::Blueprint::Wire w;
        w.id = next_unique_id(interner, used_wire_ids, "extract_wire_");
        used_wire_ids.insert(w.id);
        w.domain = ec.domain;
        w.source = arena.make_port(arena.make_node(arena.root(), ec.external_node_id), ec.external_port);
        w.target = arena.make_port(arena.make_node(arena.root(), nested_instance_id), interner.intern(ec.iface_name));
        out = out.with_wire(std::move(w));
    }
    for (const auto& ec : plan.outputs) {
        bp2::Blueprint::Wire w;
        w.id = next_unique_id(interner, used_wire_ids, "extract_wire_");
        used_wire_ids.insert(w.id);
        w.domain = ec.domain;
        w.source = arena.make_port(arena.make_node(arena.root(), nested_instance_id), interner.intern(ec.iface_name));
        w.target = arena.make_port(arena.make_node(arena.root(), ec.external_node_id), ec.external_port);
        out = out.with_wire(std::move(w));
    }

    return out;
}

} // namespace

std::optional<bp2::Blueprint> build_extracted_blueprint_atomic(
    const bp2::Blueprint& source,
    const std::vector<ui::InternedId>& selected_node_ids,
    const std::string& blueprint_name,
    const std::string& group_id,
    ui::StringInterner& interner,
    bp2::PathArena& arena,
    std::string* error_out,
    bool allow_nonembedded_descendant_refs) {
    ui::InternedId blueprint_iid;
    if (!validate_blueprint_name_for_extract(source, blueprint_name, interner, &blueprint_iid, error_out)) {
        return std::nullopt;
    }

    auto plan_opt = analyze_selection(
        source,
        selected_node_ids,
        group_id,
        allow_nonembedded_descendant_refs,
        interner,
        arena,
        error_out);
    if (!plan_opt) return std::nullopt;
    const ExtractionPlan& plan = *plan_opt;

    auto out_opt = build_parent_blueprint_from_plan(
        source,
        plan,
        blueprint_iid,
        blueprint_name,
        group_id,
        allow_nonembedded_descendant_refs,
        interner,
        arena,
        error_out);
    if (!out_opt) return std::nullopt;
    bp2::Blueprint out = std::move(*out_opt);

    std::string integrity_err;
    if (!validate_blueprint_integrity(out, interner, arena, &integrity_err)) {
        if (error_out) *error_out = integrity_err;
        return std::nullopt;
    }

    if (error_out) error_out->clear();
    return out;
}

std::optional<ExtractToBlueprintPreview> build_extract_to_blueprint_preview(
    const bp2::Blueprint& source,
    const std::vector<ui::InternedId>& selected_node_ids,
    const std::string& blueprint_name,
    const std::string& group_id,
    ui::StringInterner& interner,
    bp2::PathArena& arena,
    std::string* error_out,
    bool allow_nonembedded_descendant_refs) {
    if (!validate_blueprint_name_for_extract(source, blueprint_name, interner, nullptr, error_out)) {
        return std::nullopt;
    }

    auto plan_opt = analyze_selection(
        source,
        selected_node_ids,
        group_id,
        allow_nonembedded_descendant_refs,
        interner,
        arena,
        error_out);
    if (!plan_opt) return std::nullopt;

    const ExtractionPlan& plan = *plan_opt;
    ExtractToBlueprintPreview out;
    out.selected_nodes = plan.internal_nodes.size();
    out.internal_wires = plan.internal_wires.size();
    out.input_count = plan.inputs.size();
    out.output_count = plan.outputs.size();
    const DescendantRemapStats remap_stats =
        collect_descendant_remap_stats(source, plan.selected_set, allow_nonembedded_descendant_refs);
    out.remapped_descendant_refs = remap_stats.remapped;
    out.passthrough_descendant_refs = remap_stats.passthrough;
    out.input_iface_names.reserve(plan.inputs.size());
    out.output_iface_names.reserve(plan.outputs.size());

    std::unordered_set<std::string> in_names;
    in_names.reserve(plan.inputs.size());
    for (const auto& ec : plan.inputs) {
        out.input_iface_names.push_back(ec.iface_name);
        in_names.insert(ec.iface_name);
    }
    for (const auto& ec : plan.outputs) {
        out.output_iface_names.push_back(ec.iface_name);
        if (in_names.find(ec.iface_name) != in_names.end()) {
            out.iface_collision_names.push_back(ec.iface_name);
        }
    }
    std::sort(out.iface_collision_names.begin(), out.iface_collision_names.end());
    out.iface_collision_names.erase(
        std::unique(out.iface_collision_names.begin(), out.iface_collision_names.end()),
        out.iface_collision_names.end());

    if (error_out) error_out->clear();
    return out;
}

} // namespace editor::commands
