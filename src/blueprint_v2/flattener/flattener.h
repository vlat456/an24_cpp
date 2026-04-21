#pragma once

#include "flat_netlist.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/library/blueprint_library.h"
#include "blueprint_v2/path/path.h"
#include <unordered_map>

namespace bp2 {

class Flattener {
public:
    explicit Flattener(BlueprintLibrary const& library);

    FlatNetlist flatten(Blueprint const& root, PathArena& arena);

private:
    BlueprintLibrary const& library_;
    PathArena* arena_ = nullptr;

    [[noreturn]] void throw_unresolved_blueprint_instance(Blueprint::Node const& node, Path prefix) const;

    void visit_blueprint(
        Blueprint const& bp,
        Path prefix,
        std::unordered_map<Path, SignalIndex>& signals,
        FlatNetlist& out);

    void emit_component(
        Blueprint const& bp,
        Blueprint::Node const& node,
        Path prefix,
        std::unordered_map<Path, SignalIndex>& signals,
        FlatNetlist& out);

    void process_wires(
        Blueprint const& bp,
        Path prefix,
        std::unordered_map<Path, SignalIndex>& signals,
        FlatNetlist& out);

    void visit_blueprint_instance(
        Blueprint::Node const& node,
        Path prefix,
        std::unordered_map<Path, SignalIndex>& signals,
        FlatNetlist& out);

    /// Resolve a wire endpoint within a scope blueprint.
    ///
    /// If ep.node refers to a leaf component, returns prefix / node / port.
    /// If ep.node refers to a blueprint_instance, resolves through to the
    /// inner bridge node's ext port: prefix / instance / bridge_node / ext.
    Path resolve_endpoint(
        Blueprint const& scope_bp,
        Path scope_prefix,
        WireEndpoint const& ep);

    /// Find the bridge node inside a blueprint's nodes that corresponds
    /// to an interface port name. Authoritative match on exposed_port only.
    Blueprint::Node const* find_bridge_for_port(
        Blueprint const& inner_bp,
        ui::InternedId port_name) const;

    SignalIndex get_or_create_signal(
        Path port_path,
        Domain domain,
        std::unordered_map<Path, SignalIndex>& signals,
        FlatNetlist& out);

    void merge_signals(
        SignalIndex keep,
        SignalIndex remove,
        std::unordered_map<Path, SignalIndex>& signals,
        FlatNetlist& out);
};

} // namespace bp2
