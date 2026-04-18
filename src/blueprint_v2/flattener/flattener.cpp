#include "flattener.h"

#include <stdexcept>

namespace bp2 {

namespace {

ui::InternedId find_exposed_port_name_for_bridge(const Blueprint& bp,
                                                 const Blueprint::Node& node,
                                                 const PathArena& arena) {
    (void)bp;
    (void)arena;
    return node.bridge_port().exposed_port;
}

} // namespace

Flattener::Flattener(BlueprintLibrary const& library)
    : library_(library) {}

FlatNetlist Flattener::flatten(Blueprint const& root, PathArena& arena) {
    arena_ = &arena;
    FlatNetlist out;
    std::unordered_map<Path, SignalIndex> signals;

    process_wires(root, arena_->root(), signals, out);
    visit_blueprint(root, arena_->root(), signals, out);

    return out;
}

// ==================================================================
// throw_unresolved_blueprint_instance — fail loudly on missing bp definition
// ==================================================================

[[noreturn]] void Flattener::throw_unresolved_blueprint_instance(
    Blueprint::Node const& node, Path prefix) const {
    const std::string instance_path = arena_->to_string(arena_->make_node(prefix, node.semantic.id));
    const auto bp_id = node.blueprint_instance().source.blueprint_id();
    const auto bp_name = arena_->resolve_id(bp_id);
    const std::string blueprint_id = bp_name.empty()
        ? std::string{"<empty>"}
        : std::string{bp_name};
    throw std::logic_error(
        "Flattener: unresolved blueprint for instance '" + instance_path
        + "' (blueprint_id='" + blueprint_id + "')");
}

// ==================================================================
// find_bridge_for_port — locate the bridge node matching an interface port
// ==================================================================

Blueprint::Node const* Flattener::find_bridge_for_port(
    Blueprint const& inner_bp,
    ui::InternedId port_name) const {
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
    WireEndpoint const& ep) {

    auto const* node = scope_bp.find_node(ep.node);
    if (!node || !node->is_blueprint_instance()) {
        // Leaf component — straightforward path
        Path node_path = arena_->make_node(scope_prefix, ep.node);
        return arena_->make_port(node_path, ep.port);
    }

    // Blueprint instance — resolve through to bridge's ext port
    Path instance_path = arena_->make_node(scope_prefix, ep.node);

    Blueprint const* inner = nullptr;
    if (auto* def = node->blueprint_instance().source.inline_def()) {
        inner = def;
    } else {
        inner = library_.find(node->blueprint_instance().source.blueprint_id());
    }
    if (!inner) {
        throw_unresolved_blueprint_instance(*node, scope_prefix);
    }

    Blueprint::Node const* bridge = find_bridge_for_port(*inner, ep.port);
    if (!bridge) {
        std::string inst_str(arena_->resolve_id(ep.node));
        std::string port_str(arena_->resolve_id(ep.port));
        throw std::logic_error(
            "Flattener: no bridge node found for interface port '" + port_str
            + "' in blueprint instance '" + inst_str + "'");
    }

    Path bridge_path = arena_->make_node(instance_path, bridge->semantic.id);

    // Find the "ext" port ID from the bridge node's interface
    ui::InternedId ext_port_id{};
    for (auto const& p : inner->effective_node_iface(*bridge)) {
        std::string_view pname = arena_->resolve_id(p.name);
        if (pname == "ext") {
            ext_port_id = p.name;
            break;
        }
    }
    if (ext_port_id == ui::InternedId{}) {
        std::string bridge_str(arena_->resolve_id(bridge->semantic.id));
        throw std::logic_error(
            "Flattener: bridge node '" + bridge_str + "' has no 'ext' port");
    }

    return arena_->make_port(bridge_path, ext_port_id);
}

// ==================================================================
// visit_blueprint — emit leaf nodes, recurse into blueprint instances
// ==================================================================

void Flattener::visit_blueprint(
    Blueprint const& bp,
    Path prefix,
    std::unordered_map<Path, SignalIndex>& signals,
    FlatNetlist& out) {

    for (auto const& node : bp.nodes()) {
        if (node.is_blueprint_instance()) {
            visit_blueprint_instance(node, prefix, signals, out);
        } else {
            emit_component(bp, node, prefix, signals, out);
        }
    }
}

// ==================================================================
// emit_component — create a FlatNetlist::Component for a leaf node
//
// For structural bridge nodes, after emitting
// the component we unify the ext and port signals, since they
// represent the same electrical point (the bridge is a pass-through).
// ==================================================================

void Flattener::emit_component(
    Blueprint const& bp,
    Blueprint::Node const& node,
    Path prefix,
    std::unordered_map<Path, SignalIndex>& signals,
    FlatNetlist& out) {

    Path node_path = arena_->make_node(prefix, node.semantic.id);

    FlatNetlist::Component comp;
    comp.path = node_path;
    comp.type = node.semantic.type;
    comp.exposed_port_name = {};
    comp.params = node.semantic.params;
    comp.string_params = node.semantic.string_params;

    if (node.is_bridge_port()) {
        // Preserve the authoritative public interface port id represented by
        // this structural bridge node.
        comp.exposed_port_name = find_exposed_port_name_for_bridge(bp, node, *arena_);
    }

    SignalIndex ext_sig = UINT32_MAX;
    SignalIndex port_sig = UINT32_MAX;

    for (auto const& port : bp.effective_node_iface(node)) {
        Path port_path = arena_->make_port(node_path, port.name);
        SignalIndex sig = get_or_create_signal(
            port_path, port.domain, signals, out);
        comp.ports.push_back(port);
        comp.port_signals.push_back({port.name, sig});

        // Track ext/port signals for bridge unification
        std::string_view pname = arena_->resolve_id(port.name);
        if (pname == "ext") ext_sig = sig;
        else if (pname == "port") port_sig = sig;
    }

    out.components.push_back(std::move(comp));

    // Bridge ext/port unification: merge the two signals so that
    // wires to bridge.ext and bridge.port end up on the same signal
    if (node.is_bridge_port()
        && ext_sig != UINT32_MAX && port_sig != UINT32_MAX
        && ext_sig != port_sig) {
        merge_signals(ext_sig, port_sig, signals, out);
    }
}

// ==================================================================
// process_wires — pre-allocate signals for wire endpoints, merge
// ==================================================================

void Flattener::process_wires(
    Blueprint const& bp,
    Path prefix,
    std::unordered_map<Path, SignalIndex>& signals,
    FlatNetlist& out) {

    for (auto const& wire : bp.wires()) {
        SignalIndex src_sig = get_or_create_signal(
            resolve_endpoint(bp, prefix, wire.source),
            wire.domain, signals, out);
        SignalIndex tgt_sig = get_or_create_signal(
            resolve_endpoint(bp, prefix, wire.target),
            wire.domain, signals, out);

        if (src_sig != tgt_sig) {
            merge_signals(src_sig, tgt_sig, signals, out);
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
    FlatNetlist& out) {

    Path node_path = arena_->make_node(prefix, node.semantic.id);

    Blueprint const* inner = nullptr;
    if (auto* def = node.blueprint_instance().source.inline_def()) {
        inner = def;
    } else {
        inner = library_.find(node.blueprint_instance().source.blueprint_id());
    }
    if (!inner) {
        throw_unresolved_blueprint_instance(node, prefix);
    }

    // Seed boundary signals: for each interface port, find the bridge node
    // and seed its ext path with the parent signal (if wired from outside).
    //
    // The outer wires (processed by the parent scope's process_wires) have
    // already resolved to instance/bridge/ext via resolve_endpoint. We look
    // up those paths in the parent signals map and propagate them into the
    // nested scope.
    std::unordered_map<Path, SignalIndex> nested_signals;
    std::unordered_map<Path, SignalIndex> seeded_boundary;
    for (auto const& port : node.blueprint_instance().source.cached_iface()) {
        Blueprint::Node const* bridge = find_bridge_for_port(*inner, port.name);
        if (!bridge) continue;

        Path bridge_path = arena_->make_node(node_path, bridge->semantic.id);

        // Find the "ext" port ID from the bridge's interface
        ui::InternedId ext_id{};
        for (auto const& p : inner->effective_node_iface(*bridge)) {
            std::string_view pname = arena_->resolve_id(p.name);
            if (pname == "ext") {
                ext_id = p.name;
                break;
            }
        }
        if (ext_id == ui::InternedId{}) continue;

        Path ext_path = arena_->make_port(bridge_path, ext_id);
        auto it = signals.find(ext_path);
        if (it != signals.end()) {
            nested_signals[ext_path] = it->second;
            seeded_boundary[ext_path] = it->second;
        }
    }

    // Process inner wires with paths resolved under node_path.
    // resolve_endpoint handles nested blueprint instances recursively.
    for (auto const& wire : inner->wires()) {
        Path src = resolve_endpoint(*inner, node_path, wire.source);
        Path tgt = resolve_endpoint(*inner, node_path, wire.target);

        SignalIndex src_sig = get_or_create_signal(
            src, wire.domain, nested_signals, out);
        SignalIndex tgt_sig = get_or_create_signal(
            tgt, wire.domain, nested_signals, out);

        if (src_sig != tgt_sig) {
            merge_signals(src_sig, tgt_sig, nested_signals, out);
        }
    }

    // Propagate boundary merges back to parent signal map.
    // If a seeded boundary signal was merged into a different index,
    // we must apply that same merge in the parent signals map.
    for (auto const& [port_path, original_sig] : seeded_boundary) {
        SignalIndex current_sig = nested_signals[port_path];
        if (current_sig != original_sig) {
            merge_signals(current_sig, original_sig, signals, out);
        }
    }

    // Merge nested signal map back into parent
    for (auto const& [path, sig] : nested_signals) {
        signals[path] = sig;
    }

    // Emit inner leaf nodes and recurse into inner blueprint instances
    visit_blueprint(*inner, node_path, nested_signals, out);
}

// ==================================================================
// get_or_create_signal — find existing or allocate new signal
// ==================================================================

SignalIndex Flattener::get_or_create_signal(
    Path port_path,
    Domain domain,
    std::unordered_map<Path, SignalIndex>& signals,
    FlatNetlist& out) {

    auto it = signals.find(port_path);
    if (it != signals.end()) return it->second;

    SignalIndex idx = out.signal_count++;
    signals[port_path] = idx;

    FlatNetlist::Signal sig;
    sig.index = idx;
    sig.domain = domain;
    sig.connected_ports.push_back(port_path);
    out.signals.push_back(std::move(sig));

    return idx;
}

// ==================================================================
// merge_signals — rewrite all references from 'remove' to 'keep'
// ==================================================================

void Flattener::merge_signals(
    SignalIndex keep,
    SignalIndex remove,
    std::unordered_map<Path, SignalIndex>& signals,
    FlatNetlist& out) {

    if (keep == remove) return;

    for (auto& [path, sig] : signals) {
        if (sig == remove) sig = keep;
    }

    // Rewrite already-emitted component port_signals
    for (auto& comp : out.components) {
        for (auto& [name, sig] : comp.port_signals) {
            if (sig == remove) sig = keep;
        }
    }

    auto& keep_sig = out.signals[keep];
    auto& remove_sig = out.signals[remove];
    for (auto& p : remove_sig.connected_ports) {
        keep_sig.connected_ports.push_back(p);
    }
    remove_sig.connected_ports.clear();
}

} // namespace bp2
