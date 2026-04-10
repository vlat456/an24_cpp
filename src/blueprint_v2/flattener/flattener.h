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

    Path remap_path(Path inner_path, Path nested_prefix);

    Path remap_endpoint(WireEndpoint const& ep, Path nested_prefix);

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
