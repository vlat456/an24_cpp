#include "flattener.h"

namespace bp2 {

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
    const auto bp_id = node.source->blueprint_id();
    const auto bp_name = arena_->resolve_id(bp_id);
    const std::string blueprint_id = bp_name.empty()
        ? std::string{"<empty>"}
        : std::string{bp_name};
    throw std::logic_error(
        "Flattener: unresolved blueprint for instance '" + instance_path
        + "' (blueprint_id='" + blueprint_id + "')");
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
    comp.params = node.semantic.params;
    comp.string_params = node.semantic.string_params;

    for (auto const& port : bp.effective_node_iface(node)) {
        Path port_path = arena_->make_port(node_path, port.name);
        SignalIndex sig = get_or_create_signal(
            port_path, port.domain, signals, out);
        comp.ports.push_back(port);
        comp.port_signals.push_back({port.name, sig});
    }

    out.components.push_back(std::move(comp));
}

// ==================================================================
// process_wires — pre-allocate signals for wire endpoints, merge
// ==================================================================

void Flattener::process_wires(
    Blueprint const& bp,
    Path /*prefix*/,
    std::unordered_map<Path, SignalIndex>& signals,
    FlatNetlist& out) {

    for (auto const& wire : bp.wires()) {
        SignalIndex src_sig = get_or_create_signal(
            wire.source.to_path(*arena_), wire.domain, signals, out);
        SignalIndex tgt_sig = get_or_create_signal(
            wire.target.to_path(*arena_), wire.domain, signals, out);

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

    if (!node.source) {
        throw std::logic_error("Flattener: blueprint-instance node without source");
    }

    Path node_path = arena_->make_node(prefix, node.semantic.id);

    Blueprint const* inner = nullptr;
    if (auto* def = node.source->inline_def()) {
        inner = def;
    } else {
        inner = library_.find(node.source->blueprint_id());
    }
    if (!inner) {
        throw_unresolved_blueprint_instance(node, prefix);
    }

    // Seed boundary signals: map outer port path to parent signal index
    std::unordered_map<Path, SignalIndex> nested_signals;
    std::unordered_map<Path, SignalIndex> seeded_boundary;
    for (auto const& port : node.source->cached_iface()) {
        Path outer_port = arena_->make_port(node_path, port.name);
        auto it = signals.find(outer_port);
        if (it != signals.end()) {
            nested_signals[outer_port] = it->second;
            seeded_boundary[outer_port] = it->second;
        }
    }

    // Process inner wires with paths remapped under node_path
    for (auto const& wire : inner->wires()) {
        Path src = remap_endpoint(wire.source, node_path);
        Path tgt = remap_endpoint(wire.target, node_path);

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
// remap_path — prefix an inner-relative path with nested instance path
// ==================================================================

Path Flattener::remap_path(Path inner_path, Path nested_prefix) {
    if (inner_path.kind() == PathKind::Root) {
        return nested_prefix;
    }

    std::vector<std::pair<PathKind, ui::InternedId>> segments;
    Path current = inner_path;
    while (current.kind() != PathKind::Root) {
        segments.push_back({current.kind(), current.segment()});
        current = arena_->parent(current);
    }

    Path result = nested_prefix;
    for (auto it = segments.rbegin(); it != segments.rend(); ++it) {
        switch (it->first) {
            case PathKind::Node:
                result = arena_->make_node(result, it->second);
                break;
            case PathKind::Port:
                result = arena_->make_port(result, it->second);
                break;
            case PathKind::Nested:
                result = arena_->make_nested(result, it->second);
                break;
            case PathKind::Wire:
                result = arena_->make_wire(result, it->second);
                break;
            default:
                break;
        }
    }
    return result;
}

// ==================================================================
// remap_endpoint — materialize a WireEndpoint under a nested prefix
// ==================================================================

Path Flattener::remap_endpoint(WireEndpoint const& ep, Path nested_prefix) {
    Path node_path = arena_->make_node(nested_prefix, ep.node);
    return arena_->make_port(node_path, ep.port);
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
