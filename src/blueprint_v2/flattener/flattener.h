#pragma once

#include "flat_netlist.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/library/blueprint_library.h"
#include "blueprint_v2/path/path.h"
#include <unordered_map>

namespace core::utils { class UnionFind; }

namespace bp2 {

class Flattener {
public:
    explicit Flattener(BlueprintLibrary const& library);

    /// Flatten a blueprint hierarchy into a single FlatNetlist.
    /// Safe to call sequentially.
    FlatNetlist flatten(Blueprint const& root, PathArena& arena);

private:
    BlueprintLibrary const& library_;

    [[noreturn]] void throw_unresolved_blueprint_instance(Blueprint::Node const& node, Path prefix, PathArena& arena) const;
    [[noreturn]] void throw_invalid_endpoint(Blueprint const& scope_bp,
                                            WireEndpoint const& ep,
                                            const char* reason, PathArena& arena) const;

    void visit_blueprint(
        Blueprint const& bp,
        Path prefix,
        std::unordered_map<Path, SignalIndex>& signals,
        core::utils::UnionFind& uf,
        FlatNetlist& out,
        PathArena& arena);

    void emit_component(
        Blueprint const& bp,
        Blueprint::Node const& node,
        Path prefix,
        std::unordered_map<Path, SignalIndex>& signals,
        core::utils::UnionFind& uf,
        FlatNetlist& out,
        PathArena& arena);

    void process_wires(
        Blueprint const& bp,
        Path prefix,
        std::unordered_map<Path, SignalIndex>& signals,
        core::utils::UnionFind& uf,
        FlatNetlist& out,
        PathArena& arena);

    void visit_blueprint_instance(
        Blueprint::Node const& node,
        Path prefix,
        std::unordered_map<Path, SignalIndex>& signals,
        core::utils::UnionFind& uf,
        FlatNetlist& out,
        PathArena& arena);

    /// Resolve a wire endpoint within a scope blueprint.
    Path resolve_endpoint(
        Blueprint const& scope_bp,
        Path scope_prefix,
        WireEndpoint const& ep,
        PathArena& arena);

    /// Find the bridge node inside a blueprint's nodes that corresponds
    /// to an interface port name. Authoritative match on exposed_port only.
    Blueprint::Node const* find_bridge_for_port(
        Blueprint const& inner_bp,
        ui::InternedId port_name) const;

    /// Allocate a new provisional signal index, or return existing one.
    SignalIndex get_or_create_signal(
        Path port_path,
        Domain domain,
        std::unordered_map<Path, SignalIndex>& signals,
        core::utils::UnionFind& uf,
        FlatNetlist& out);

    /// Post-expansion: remap provisional signal indices to compact dense range
    /// using UnionFind roots. Rebuilds out.signals with grouped connected_ports.
    void compact_signals(core::utils::UnionFind& uf, FlatNetlist& out);
};

} // namespace bp2
