#include "blueprint_bridge.h"
#include "editor/data/flat_blueprint.h"
#include "json_parser/json_parser.h"
#include "blueprint_v2/path/path.h"
#include "blueprint_v2/registry/type_registry.h"
#include <map>
#include <optional>

namespace bp2 {

Blueprint BlueprintBridge::from_flat(FlatBlueprint const& flat,
                                     ui::StringInterner& interner) {
    Blueprint bp;
    bp = bp.with_id(interner.intern(flat.meta.name));
    bp = bp.with_display_name(flat.meta.name);

    PathArena arena(interner);

    // Convert nodes
    for (auto const& [node_id, fn] : flat.nodes) {
        Blueprint::Node node;
        node.id = interner.intern(node_id);
        node.type = interner.intern(fn.type);
        if (fn.pos.size() >= 2) {
            node.x = fn.pos[0];
            node.y = fn.pos[1];
        }
        // Convert string params to InternedId params
        for (auto const& [k, v] : fn.params) {
            node.params[interner.intern(k)] = std::stof(v);
        }
        bp = bp.with_node(std::move(node));
    }

    // Convert wires
    for (auto const& fw : flat.wires) {
        Blueprint::Wire wire;
        wire.id = interner.intern(fw.id);
        auto src = arena.parse(fw.from.node + ":" + fw.from.port);
        auto tgt = arena.parse(fw.to.node + ":" + fw.to.port);
        if (src) wire.source = *src;
        if (tgt) wire.target = *tgt;
        wire.domain = Domain::Electrical;
        bp = bp.with_wire(std::move(wire));
    }

    // Convert nested (sub_blueprints)
    for (auto const& [sub_id, sub] : flat.sub_blueprints) {
        Blueprint::Nested nested;
        nested.id = interner.intern(sub_id);
        nested.blueprint_id = interner.intern(sub.type_name);
        nested.embedded = sub.is_embedded();
        if (sub.pos.size() >= 2) {
            nested.x = sub.pos[0];
            nested.y = sub.pos[1];
        }
        bp = bp.with_nested(std::move(nested));
    }

    // Convert interface (exposes)
    std::vector<PortDescriptor> ports;
    for (auto const& [name, port] : flat.exposes) {
        Direction dir = Direction::Input;
        if (port.direction == "Out") dir = Direction::Output;
        else if (port.direction == "InOut") dir = Direction::InOut;
        Domain dom = Domain::Electrical;
        ports.push_back({interner.intern(name), dom, dir});
    }
    if (!ports.empty()) {
        bp = bp.with_interface(Interface(ports));
    }

    return bp;
}

static FlatPort direction_to_flat_port(Direction dir) {
    FlatPort fp;
    if (dir == Direction::Input) fp.direction = "In";
    else if (dir == Direction::Output) fp.direction = "Out";
    else fp.direction = "InOut";
    return fp;
}

FlatBlueprint BlueprintBridge::to_flat(Blueprint const& bp,
                                       ui::StringInterner& interner,
                                       PathArena& arena) {
    FlatBlueprint flat;
    flat.meta.name = std::string(interner.resolve(bp.id()));
    flat.meta.description = bp.display_name();

    // Convert nodes
    for (auto const& node : bp.nodes()) {
        FlatNode fn;
        fn.type = std::string(interner.resolve(node.type));
        fn.pos = {node.x, node.y};
        for (auto const& [k, v] : node.params) {
            fn.params[std::string(interner.resolve(k))] = std::to_string(v);
        }
        flat.nodes[std::string(interner.resolve(node.id))] = std::move(fn);
    }

    // Convert wires
    for (auto const& wire : bp.wires()) {
        FlatWire fw;
        fw.id = std::string(interner.resolve(wire.id));
        auto src_node = arena.parent(wire.source);
        fw.from.node = std::string(interner.resolve(src_node.segment()));
        fw.from.port = std::string(interner.resolve(wire.source.segment()));
        auto tgt_node = arena.parent(wire.target);
        fw.to.node = std::string(interner.resolve(tgt_node.segment()));
        fw.to.port = std::string(interner.resolve(wire.target.segment()));
        flat.wires.push_back(std::move(fw));
    }

    // Convert nested
    for (auto const& nested : bp.nested()) {
        FlatSubBlueprint sub;
        sub.type_name = std::string(interner.resolve(nested.blueprint_id));
        sub.pos = {nested.x, nested.y};
        sub.collapsed = !nested.embedded;
        flat.sub_blueprints[std::string(interner.resolve(nested.id))] = std::move(sub);
    }

    // Convert interface
    for (auto const& port : bp.iface()) {
        std::string name = std::string(interner.resolve(port.name));
        flat.exposes[name] = direction_to_flat_port(port.direction);
    }

    return flat;
}

Blueprint BlueprintBridge::from_type_def(TypeDefinition const& td,
                                        ui::StringInterner& interner,
                                        TypeRegistry& registry) {
    Blueprint bp;
    bp = bp.with_id(interner.intern(td.classname));
    bp = bp.with_display_name(td.classname);

    // Convert ports to interface
    std::vector<PortDescriptor> ports;
    for (auto const& [name, port] : td.ports) {
        Direction dir = Direction::Input;
        if (port.direction == PortDirection::Out) dir = Direction::Output;
        else if (port.direction == PortDirection::InOut) dir = Direction::InOut;
        ports.push_back({interner.intern(name), Domain::Electrical, dir});
    }
    if (!ports.empty()) {
        bp = bp.with_interface(Interface(ports));
    }

    // Register as a component in the registry
    registry.register_component(
        interner.intern(td.classname),
        bp.iface(),
        td.description
    );

    return bp;
}

TypeDefinition BlueprintBridge::to_type_def(Blueprint const& bp,
                                            ui::StringInterner& interner) {
    TypeDefinition td;
    td.classname = std::string(interner.resolve(bp.id()));
    td.description = bp.display_name();
    td.cpp_class = true;

    // Convert interface to ports
    for (auto const& port : bp.iface()) {
        std::string name = std::string(interner.resolve(port.name));
        PortDirection dir = PortDirection::In;
        if (port.direction == Direction::Output) dir = PortDirection::Out;
        else if (port.direction == Direction::InOut) dir = PortDirection::InOut;
        td.ports[name] = Port{dir, PortType::Any, std::nullopt};
    }

    return td;
}

} // namespace bp2
