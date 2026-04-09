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
// throw_unresolved_nested — fail loudly on missing nested blueprint
// ==================================================================

[[noreturn]] void Flattener::throw_unresolved_nested(Blueprint::Nested const& nested, Path prefix) const {
    const std::string instance_path = arena_->to_string(arena_->make_nested(prefix, nested.id));
    const auto bp_id = nested.blueprint_id();
    const auto bp_name = arena_->resolve_id(bp_id);
    const std::string blueprint_id = bp_name.empty()
        ? std::string{"<empty>"}
        : std::string{bp_name};
    throw std::logic_error(
        "Flattener: unresolved nested blueprint for instance '" + instance_path
        + "' (blueprint_id='" + blueprint_id + "')");
}

// ==================================================================
// visit_blueprint — emit leaf nodes, recurse into nested instances
// ==================================================================

void Flattener::visit_blueprint(
    Blueprint const& bp,
    Path prefix,
    std::unordered_map<Path, SignalIndex>& signals,
    FlatNetlist& out) {

    for (auto const& node : bp.nodes()) {
        if (bp.find_hosted_nested(node)) {
            continue;
        }
        emit_component(bp, node, prefix, signals, out);
    }

    for (auto const& nested : bp.nested()) {
        visit_nested(nested, prefix, signals, out);
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
    comp.render_hint = node.view.render_hint;

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
            wire.source, wire.domain, signals, out);
        SignalIndex tgt_sig = get_or_create_signal(
            wire.target, wire.domain, signals, out);

        if (src_sig != tgt_sig) {
            merge_signals(src_sig, tgt_sig, signals, out);
        }
    }
}

// ==================================================================
// visit_nested — expand a nested blueprint instance recursively
// ==================================================================

void Flattener::visit_nested(
    Blueprint::Nested const& nested,
    Path prefix,
    std::unordered_map<Path, SignalIndex>& signals,
    FlatNetlist& out) {

    Path nested_path = arena_->make_nested(prefix, nested.id);

    Blueprint const* inner = nullptr;
    if (auto* def = nested.inline_def()) {
        inner = def;
    } else {
        inner = library_.find(nested.blueprint_id());
    }
    if (!inner) {
        throw_unresolved_nested(nested, prefix);
    }

    // Seed boundary signals: map outer port path to parent signal index
    std::unordered_map<Path, SignalIndex> nested_signals;
    std::unordered_map<Path, SignalIndex> seeded_boundary;
    for (auto const& port : nested.resolved_iface()) {
        Path outer_port = arena_->make_port(nested_path, port.name);
        auto it = signals.find(outer_port);
        if (it != signals.end()) {
            nested_signals[outer_port] = it->second;
            seeded_boundary[outer_port] = it->second;
        }
    }

    // Process inner wires with paths remapped under nested_path
    for (auto const& wire : inner->wires()) {
        Path src = remap_path(wire.source, nested_path);
        Path tgt = remap_path(wire.target, nested_path);

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

    // Emit inner leaf nodes (skip composite hosts — they expand via nested)
    for (auto const& node : inner->nodes()) {
        if (inner->find_hosted_nested(node)) {
            continue;
        }
        emit_component(*inner, node, nested_path, nested_signals, out);
    }

    // Recurse into nested-of-nested
    for (auto const& inner_nested : inner->nested()) {
        visit_nested(inner_nested, nested_path, nested_signals, out);
    }
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
