#include "flattener.h"

#include "core/utils/union_find.h"

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace bp2 {

Flattener::Flattener(BlueprintLibrary const& library)
    : library_(library) {}

FlatNetlist Flattener::flatten(Blueprint const& root, PathArena& arena) {
    FlatNetlist out;
    std::unordered_map<Path, SignalIndex> signals;

    // UnionFind is scoped to a single flatten() call — no stale state on reuse.
    core::utils::UnionFind uf{0};

    process_wires(root, arena.root(), signals, uf, out, arena);
    visit_blueprint(root, arena.root(), signals, uf, out, arena);

    compact_signals(uf, out);

    return out;
}

// ==================================================================
// throw_unresolved_blueprint_instance — fail loudly on missing bp definition
// ==================================================================

[[noreturn]] void Flattener::throw_unresolved_blueprint_instance(
    Blueprint::Node const& node, Path prefix, PathArena& arena) const {
    const std::string instance_path = arena.to_string(arena.make_node(prefix, node.semantic.id));
    const auto bp_id = node.blueprint_instance().source.blueprint_id();
    const auto bp_name = arena.resolve_id(bp_id);
    const std::string blueprint_id = bp_name.empty()
        ? std::string{"<empty>"}
        : std::string{bp_name};
    throw std::logic_error(
        "Flattener: unresolved blueprint for instance '" + instance_path
        + "' (blueprint_id='" + blueprint_id + "')");
}

[[noreturn]] void Flattener::throw_invalid_endpoint(Blueprint const& scope_bp,
                                                    WireEndpoint const& ep,
                                                    const char* reason, PathArena& arena) const {
    const std::string scope_path = arena.to_string(scope_bp.id().empty()
        ? arena.root()
        : arena.make_node(arena.root(), scope_bp.id()));
    const std::string node_name = ep.node.empty()
        ? std::string{"<empty>"}
        : std::string{arena.resolve_id(ep.node)};
    const std::string port_name = ep.port.empty()
        ? std::string{"<empty>"}
        : std::string{arena.resolve_id(ep.port)};
    throw std::logic_error(
        "Flattener: invalid endpoint '" + node_name + "." + port_name
        + "' in blueprint '" + scope_path + "': " + reason);
}

// ==================================================================
// find_bridge_for_port — locate the bridge node matching an interface port
// ==================================================================

Blueprint::Node const* Flattener::find_bridge_for_port(
    Blueprint const& inner_bp,
    core::InternedId port_name) const {
    for (auto const& n : inner_bp.nodes()) {
        if (!n.is_bridge_port()) continue;
        if (n.bridge_port().exposed_port == port_name) {
            return &n;
        }
    }
    return nullptr;
}

// ==================================================================
// resolve_endpoint — scope-aware endpoint resolver
//
// If ep.node refers to a leaf component in scope_bp:
//   returns scope_prefix / node / port
//
// If ep.node refers to a blueprint_instance in scope_bp:
//   resolves through to the bridge node's ext port:
//   scope_prefix / instance / bridge_node / ext
// ==================================================================

Path Flattener::resolve_endpoint(
    Blueprint const& scope_bp,
    Path scope_prefix,
    WireEndpoint const& ep,
    PathArena& arena) {

    if (ep.node.empty()) {
        throw_invalid_endpoint(scope_bp, ep, "missing node id", arena);
    }
    if (ep.port.empty()) {
        throw_invalid_endpoint(scope_bp, ep, "missing port id", arena);
    }

    auto const* node = scope_bp.find_node(ep.node);
    if (!node) {
        throw_invalid_endpoint(scope_bp, ep, "node not found", arena);
    }
    if (!node->is_blueprint_instance()) {
        // Leaf component — straightforward path
        Path node_path = arena.make_node(scope_prefix, ep.node);
        return arena.make_port(node_path, ep.port);
    }

    // Blueprint instance — resolve through to bridge's ext port
    Path instance_path = arena.make_node(scope_prefix, ep.node);

    Blueprint const* inner = nullptr;
    if (auto* def = node->blueprint_instance().source.inline_def()) {
        inner = def;
    } else {
        inner = library_.find(node->blueprint_instance().source.blueprint_id());
    }
    if (!inner) {
        throw_unresolved_blueprint_instance(*node, scope_prefix, arena);
    }

    Blueprint::Node const* bridge = find_bridge_for_port(*inner, ep.port);
    if (!bridge) {
        std::string inst_str(arena.resolve_id(ep.node));
        std::string port_str(arena.resolve_id(ep.port));
        throw std::logic_error(
            "Flattener: no bridge node found for interface port '" + port_str
            + "' in blueprint instance '" + inst_str + "'");
    }

    Path bridge_path = arena.make_node(instance_path, bridge->semantic.id);

    // Find the "ext" port ID from the bridge node's interface
    core::InternedId ext_port_id{};
    for (auto const& p : inner->resolve_node_iface(*bridge, Blueprint::NodeIfaceAuthority{arena.interner()})) {
        std::string_view pname = arena.resolve_id(p.name);
        if (pname == "ext") {
            ext_port_id = p.name;
            break;
        }
    }
    if (ext_port_id == core::InternedId{}) {
        std::string bridge_str(arena.resolve_id(bridge->semantic.id));
        throw std::logic_error(
            "Flattener: bridge node '" + bridge_str + "' has no 'ext' port");
    }

    return arena.make_port(bridge_path, ext_port_id);
}

// ==================================================================
// visit_blueprint — emit leaf nodes, recurse into blueprint instances
// ==================================================================

void Flattener::visit_blueprint(
    Blueprint const& bp,
    Path prefix,
    std::unordered_map<Path, SignalIndex>& signals,
    core::utils::UnionFind& uf,
    FlatNetlist& out,
    PathArena& arena) {

    for (auto const& node : bp.nodes()) {
        if (node.is_blueprint_instance()) {
            visit_blueprint_instance(node, prefix, signals, uf, out, arena);
        } else {
            emit_component(bp, node, prefix, signals, uf, out, arena);
        }
    }
}

// ==================================================================
// emit_component — create a FlatNetlist::Component for a leaf node
//
// For structural bridge nodes, after emitting the component we
// record a UnionFind union between ext and port signals, since they
// represent the same electrical point (the bridge is a pass-through).
// ==================================================================

void Flattener::emit_component(
    Blueprint const& bp,
    Blueprint::Node const& node,
    Path prefix,
    std::unordered_map<Path, SignalIndex>& signals,
    core::utils::UnionFind& uf,
    FlatNetlist& out,
    PathArena& arena) {

    Path node_path = arena.make_node(prefix, node.semantic.id);

    FlatNetlist::Component comp;
    comp.path = node_path;
    comp.type = node.semantic.type;
    comp.exposed_port_name = {};
    comp.params = node.semantic.params;
    comp.string_params = node.semantic.string_params;

    if (node.is_bridge_port()) {
        comp.exposed_port_name = node.bridge_port().exposed_port;
    }

    SignalIndex ext_sig = UINT32_MAX;
    SignalIndex port_sig = UINT32_MAX;

    for (auto const& port : bp.resolve_node_iface(node, Blueprint::NodeIfaceAuthority{arena.interner()})) {
        Path port_path = arena.make_port(node_path, port.name);
        SignalIndex sig = get_or_create_signal(
            port_path, port.domain, signals, uf, out);
        comp.ports.push_back(port);
        comp.port_signals.push_back({port.name, sig});

        std::string_view pname = arena.resolve_id(port.name);
        if (pname == "ext") ext_sig = sig;
        else if (pname == "port") port_sig = sig;
    }

    out.components.push_back(std::move(comp));

    // Bridge ext/port unification: these two signals are the same electrical point
    if (node.is_bridge_port()
        && ext_sig != UINT32_MAX && port_sig != UINT32_MAX
        && ext_sig != port_sig) {
        uf.unite(ext_sig, port_sig);
    }
}

// ==================================================================
// process_wires — record unions for wire endpoints
// ==================================================================

void Flattener::process_wires(
    Blueprint const& bp,
    Path prefix,
    std::unordered_map<Path, SignalIndex>& signals,
    core::utils::UnionFind& uf,
    FlatNetlist& out,
    PathArena& arena) {

    for (auto const& wire : bp.wires()) {
        SignalIndex src_sig = get_or_create_signal(
            resolve_endpoint(bp, prefix, wire.source, arena),
            wire.domain, signals, uf, out);
        SignalIndex tgt_sig = get_or_create_signal(
            resolve_endpoint(bp, prefix, wire.target, arena),
            wire.domain, signals, uf, out);

        if (src_sig != tgt_sig) {
            uf.unite(src_sig, tgt_sig);
        }
    }
}

// ==================================================================
// visit_blueprint_instance — expand a blueprint-instance node recursively
// ==================================================================

void Flattener::visit_blueprint_instance(
    Blueprint::Node const& node,
    Path prefix,
    std::unordered_map<Path, SignalIndex>& signals,
    core::utils::UnionFind& uf,
    FlatNetlist& out,
    PathArena& arena) {

    Path node_path = arena.make_node(prefix, node.semantic.id);

    Blueprint const* inner = nullptr;
    if (auto* def = node.blueprint_instance().source.inline_def()) {
        inner = def;
    } else {
        inner = library_.find(node.blueprint_instance().source.blueprint_id());
    }
    if (!inner) {
        throw_unresolved_blueprint_instance(node, prefix, arena);
    }

    // Seed boundary signals: for each interface port, find the bridge node
    // and seed its ext path with the parent signal (if wired from outside).
    std::unordered_map<Path, SignalIndex> nested_signals;
    for (auto const& port : inner->iface()) {
        Blueprint::Node const* bridge = find_bridge_for_port(*inner, port.name);
        if (!bridge) continue;

        Path bridge_path = arena.make_node(node_path, bridge->semantic.id);

        core::InternedId ext_id{};
        for (auto const& p : inner->resolve_node_iface(*bridge, Blueprint::NodeIfaceAuthority{arena.interner()})) {
            std::string_view pname = arena.resolve_id(p.name);
            if (pname == "ext") {
                ext_id = p.name;
                break;
            }
        }
        if (ext_id == core::InternedId{}) continue;

        Path ext_path = arena.make_port(bridge_path, ext_id);
        auto it = signals.find(ext_path);
        if (it != signals.end()) {
            nested_signals[ext_path] = it->second;
        }
    }

    // Process inner wires with paths resolved under node_path.
    for (auto const& wire : inner->wires()) {
        Path src = resolve_endpoint(*inner, node_path, wire.source, arena);
        Path tgt = resolve_endpoint(*inner, node_path, wire.target, arena);

        SignalIndex src_sig = get_or_create_signal(
            src, wire.domain, nested_signals, uf, out);
        SignalIndex tgt_sig = get_or_create_signal(
            tgt, wire.domain, nested_signals, uf, out);

        if (src_sig != tgt_sig) {
            uf.unite(src_sig, tgt_sig);
        }
    }

    // Merge nested signal map back into parent so that sibling components
    // at the same scope level can reference inner paths.
    for (auto const& [path, sig] : nested_signals) {
        signals[path] = sig;
    }

    // Emit inner leaf nodes and recurse into inner blueprint instances
    visit_blueprint(*inner, node_path, nested_signals, uf, out, arena);
}

// ==================================================================
// get_or_create_signal — find existing or allocate new provisional signal
// ==================================================================

SignalIndex Flattener::get_or_create_signal(
    Path port_path,
    Domain domain,
    std::unordered_map<Path, SignalIndex>& signals,
    core::utils::UnionFind& uf,
    FlatNetlist& out) {

    auto it = signals.find(port_path);
    if (it != signals.end()) return it->second;

    SignalIndex idx = out.signal_count++;
    signals[port_path] = idx;

    // Grow UnionFind to cover the new provisional index
    if (idx >= static_cast<uint32_t>(uf.size())) {
        uf.grow(idx + 1);
    }

    FlatNetlist::Signal sig;
    sig.index = idx;
    sig.domain = domain;
    sig.connected_ports.push_back(port_path);
    out.signals.push_back(std::move(sig));

    return idx;
}

// ==================================================================
// compact_signals — remap provisional indices to dense compact range
//
// After graph expansion, provisional signal indices may be unified
// via UnionFind. This pass:
//   1. Finds UF roots for each component port signal
//   2. Assigns compact indices in first-encounter order
//   3. Rebuilds the signals vector with grouped connected_ports
// ==================================================================

void Flattener::compact_signals(core::utils::UnionFind& uf, FlatNetlist& out) {
    // Phase 1: Assign compact indices in first-encounter order.
    // This matches the ordering that elaborate_for_jit expects,
    // making its remap pass an identity mapping.
    std::unordered_map<uint32_t, uint32_t> root_to_compact;
    uint32_t next_compact = 0;

    for (auto& comp : out.components) {
        for (auto& [name, sig] : comp.port_signals) {
            uint32_t root = uf.find(sig);
            auto [it, inserted] = root_to_compact.emplace(root, next_compact);
            if (inserted) next_compact++;
            sig = it->second;
        }
    }

    // Phase 2: Rebuild signals vector from provisional data,
    // grouping connected_ports by compact index.
    // First domain wins: the path-based lookup in get_or_create_signal
    // already ensures the first allocation determines the domain; subsequent
    // lookups return the existing signal.  Bridges may resolve to a different
    // domain than the wire that first created the path (e.g. PortType::V →
    // Electrical vs wire.domain=Logical), so last-wins would be wrong.
    std::vector<FlatNetlist::Signal> compacted(next_compact);
    for (auto& orig : out.signals) {
        uint32_t root = uf.find(orig.index);
        auto it = root_to_compact.find(root);
        if (it == root_to_compact.end()) continue;  // orphaned — no component references it
        uint32_t ci = it->second;
        if (compacted[ci].connected_ports.empty()) {
            compacted[ci].index = ci;
            compacted[ci].domain = orig.domain;
        }
        for (auto& p : orig.connected_ports) {
            compacted[ci].connected_ports.push_back(std::move(p));
        }
    }

    out.signals = std::move(compacted);
    out.signal_count = next_compact;
}

} // namespace bp2
