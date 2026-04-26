#include "signal_typing.h"

#include "blueprint_v2/interface/type_definition_interface.h"
#include "core/model/component_registry.h"
#include "core/utils/union_find.h"

#include <vector>

namespace bp2 {

namespace {

struct SignalPortRef {
    WireEndpoint endpoint;
    PortDescriptor port;
};

struct IndexedSignalGraph {
    std::vector<SignalPortRef> ports;
    std::unordered_map<WireEndpoint, uint32_t> port_to_index;
    core::utils::UnionFind uf{0};

    explicit IndexedSignalGraph(size_t count)
        : uf(count) {}
};

bool is_concrete_port_type(PortType type) {
    return type != PortType::Any && type != PortType::Contextual && type != PortType::Signal;
}

bool is_signal_port_type(PortType type) {
    return type == PortType::Signal;
}

bool is_contextual_port_type(PortType type) {
    return type == PortType::Contextual;
}

bool is_wildcard_port_type(PortType type) {
    return type == PortType::Any;
}

bool is_bridge_node(const Blueprint::Node& node,
                    core::StringInterner& /*interner*/) {
    return node.is_bridge_port();
}

bool is_bridge_port_name(core::InternedId port_name,
                         core::StringInterner& interner) {
    const std::string_view name = interner.resolve(port_name);
    return name == "ext" || name == "port";
}

std::optional<core::InternedId> bridge_exposed_port_name(const Blueprint::Node& node,
                                                       core::StringInterner& /*interner*/) {
    if (node.is_bridge_port() && !node.bridge_port().exposed_port.empty()) {
        return node.bridge_port().exposed_port;
    }
    return std::nullopt;
}

std::optional<PortDescriptor> node_port_descriptor(const Blueprint::Node& node,
                                                   core::InternedId port_name,
                                                   const Blueprint& bp,
                                                   const ComponentRegistry* parser_registry,
                                                   core::StringInterner& interner) {
    if (node.is_blueprint_instance() && parser_registry == nullptr && !node.has_embedded_blueprint()) {
        return std::nullopt;
    }

    const Interface effective_iface = bp.resolve_node_iface(
        node,
        Blueprint::NodeIfaceAuthority{interner, parser_registry});

    if (!effective_iface.empty()) {
        return effective_iface.find(port_name);
    }

    if (!node.is_component() || parser_registry == nullptr) {
        return std::nullopt;
    }

    const std::string type_name(interner.resolve(node.semantic.type));
    const auto* def = parser_registry->get(type_name);
    if (!def) {
        return std::nullopt;
    }

    const std::string port_name_str(interner.resolve(port_name));
    const auto& ports = spec_ports(*def);
    auto it = ports.find(port_name_str);
    if (it == ports.end()) {
        return std::nullopt;
    }

    auto port = port_descriptor_from_type_port(port_name, it->second);
    if (it->second.alias.has_value() && !it->second.alias->empty()) {
        port.alias = interner.intern(*it->second.alias);
    }
    return port;
}

std::optional<PortDescriptor> find_port_descriptor(const Blueprint& bp,
                                                   WireEndpoint endpoint,
                                                   const ComponentRegistry* parser_registry,
                                                   core::StringInterner& interner) {
    const auto* node = bp.find_node(endpoint.node);
    if (!node) {
        return std::nullopt;
    }
    return node_port_descriptor(*node, endpoint.port, bp, parser_registry, interner);
}

IndexedSignalGraph build_indexed_signal_graph(const Blueprint& bp,
                                              const ComponentRegistry* parser_registry,
                                              core::StringInterner& interner) {
    std::vector<SignalPortRef> ports;
    ports.reserve(bp.nodes().size() * 4 + bp.iface().size());

    for (const auto& port : bp.iface().ports()) {
        ports.push_back({WireEndpoint{core::InternedId{}, port.name}, port});
    }

    for (const auto& node : bp.nodes()) {
        if (node.is_blueprint_instance() && parser_registry == nullptr && !node.has_embedded_blueprint()) {
            continue;
        }

        const Interface effective_iface = bp.resolve_node_iface(
            node,
            Blueprint::NodeIfaceAuthority{interner, parser_registry});
        if (!effective_iface.empty()) {
            for (const auto& port : effective_iface.ports()) {
                ports.push_back({
                    WireEndpoint{node.semantic.id, port.name},
                    port,
                });
            }
            continue;
        }

        if (!node.is_component() || parser_registry == nullptr) {
            continue;
        }

        const std::string type_name(interner.resolve(node.semantic.type));
        const auto* def = parser_registry->get(type_name);
        if (!def) {
            continue;
        }

        const auto& ports_map = spec_ports(*def);
        for (const auto& [port_name, type_port] : ports_map) {
            auto port = port_descriptor_from_type_port(interner.intern(port_name), type_port);
            if (type_port.alias.has_value() && !type_port.alias->empty()) {
                port.alias = interner.intern(*type_port.alias);
            }
            ports.push_back({
                WireEndpoint{node.semantic.id, port.name},
                std::move(port),
            });
        }
    }

    IndexedSignalGraph graph(ports.size());
    graph.ports = std::move(ports);
    for (uint32_t i = 0; i < static_cast<uint32_t>(graph.ports.size()); ++i) {
        graph.port_to_index.emplace(graph.ports[i].endpoint, i);
    }

    for (const auto& [_, idx] : graph.port_to_index) {
        const auto& port = graph.ports[idx].port;
        if (!port.alias.has_value()) {
            continue;
        }
        WireEndpoint alias_ep{graph.ports[idx].endpoint.node, *port.alias};
        auto alias_it = graph.port_to_index.find(alias_ep);
        if (alias_it != graph.port_to_index.end()) {
            graph.uf.unite(idx, alias_it->second);
        }
    }

    for (const auto& node : bp.nodes()) {
        if (!is_bridge_node(node, interner)) {
            continue;
        }

        const auto exposed_name = bridge_exposed_port_name(node, interner);
        if (!exposed_name.has_value()) {
            continue;
        }

        auto root_it = graph.port_to_index.find(WireEndpoint{core::InternedId{}, *exposed_name});
        if (root_it == graph.port_to_index.end()) {
            continue;
        }

        auto ext_it = graph.port_to_index.find(WireEndpoint{node.semantic.id, interner.intern("ext")});
        auto port_it = graph.port_to_index.find(WireEndpoint{node.semantic.id, interner.intern("port")});
        if (ext_it != graph.port_to_index.end()) {
            graph.uf.unite(root_it->second, ext_it->second);
        }
        if (port_it != graph.port_to_index.end()) {
            graph.uf.unite(root_it->second, port_it->second);
        }
    }

    for (const auto& wire : bp.wires()) {
        auto src_it = graph.port_to_index.find(wire.source);
        auto tgt_it = graph.port_to_index.find(wire.target);
        if (src_it != graph.port_to_index.end() && tgt_it != graph.port_to_index.end()) {
            graph.uf.unite(src_it->second, tgt_it->second);
        }
    }

    return graph;
}

SignalTypingResult resolve_signal_group_typing(const Blueprint& bp,
                                               core::StringInterner& interner,
                                               const IndexedSignalGraph& graph,
                                               uint32_t root) {
    std::optional<Domain> concrete_domain;
    std::optional<PortType> concrete_type;
    bool saw_contextual = false;
    bool saw_signal = false;

    for (uint32_t i = 0; i < static_cast<uint32_t>(graph.ports.size()); ++i) {
        if (graph.uf.find(i) != root) {
            continue;
        }
        const auto& port = graph.ports[i].port;
        if (!graph.ports[i].endpoint.node.empty()) {
            const auto* node = bp.find_node(graph.ports[i].endpoint.node);
            if (node && is_bridge_node(*node, interner)
                && is_bridge_port_name(port.name, interner)) {
                const auto exposed_name = bridge_exposed_port_name(*node, interner);
                if (!exposed_name.has_value() || !bp.iface().has(*exposed_name)) {
                    return {{}, SignalTypingError::UnresolvedContextualSignal};
                }
            }
        }
        if (is_concrete_port_type(port.port_type)) {
            if (concrete_type.has_value() && *concrete_type != port.port_type) {
                return {{}, SignalTypingError::ConflictingConcreteTypes};
            }
            concrete_type = port.port_type;
            if (concrete_domain.has_value() && *concrete_domain != port.domain) {
                return {{}, SignalTypingError::ConflictingConcreteDomains};
            }
            concrete_domain = port.domain;
            continue;
        }
        if (is_signal_port_type(port.port_type)) {
            saw_signal = true;
            continue;
        }
        if (is_contextual_port_type(port.port_type)) {
            saw_contextual = true;
        }
    }

    // Concrete type found — it dominates Signal and Contextual.
    if (concrete_domain.has_value() && concrete_type.has_value()) {
        return {ResolvedSignalTyping{*concrete_domain, *concrete_type}, SignalTypingError::None};
    }

    // Signal type found but no more-specific concrete type — resolve as Signal.
    if (saw_signal) {
        return {ResolvedSignalTyping{Domain::Logical, PortType::Signal}, SignalTypingError::None};
    }

    if (saw_contextual) {
        return {{}, SignalTypingError::UnresolvedContextualSignal};
    }

    // Pure wildcard group: compatible but not authoritative. Preserve the
    // current permissive behavior by choosing a representative declared domain.
    for (uint32_t i = 0; i < static_cast<uint32_t>(graph.ports.size()); ++i) {
        if (graph.uf.find(i) == root) {
            return {ResolvedSignalTyping{graph.ports[i].port.domain, PortType::Any}, SignalTypingError::None};
        }
    }

    return {{}, SignalTypingError::UnknownEndpoint};
}

} // namespace

SignalTypingResult resolve_signal_typing(const Blueprint& bp,
                                         const ComponentRegistry* parser_registry,
                                         core::StringInterner& interner,
                                         WireEndpoint endpoint_a,
                                         WireEndpoint endpoint_b) {
    auto graph = build_indexed_signal_graph(bp, parser_registry, interner);

    auto it_a = graph.port_to_index.find(endpoint_a);
    if (it_a == graph.port_to_index.end()) {
        return {{}, SignalTypingError::UnknownEndpoint};
    }

    uint32_t root = graph.uf.find(it_a->second);
    if (!endpoint_b.node.empty() || !endpoint_b.port.empty()) {
        auto it_b = graph.port_to_index.find(endpoint_b);
        if (it_b == graph.port_to_index.end()) {
            return {{}, SignalTypingError::UnknownEndpoint};
        }
        graph.uf.unite(it_a->second, it_b->second);
        root = graph.uf.find(it_a->second);
    }

    return resolve_signal_group_typing(bp, interner, graph, root);
}

bool port_types_compatible(const PortDescriptor& source,
                           const PortDescriptor& target) {
    if (source.port_type == PortType::Any || target.port_type == PortType::Any) {
        return true;
    }
    if (source.port_type == PortType::Contextual || target.port_type == PortType::Contextual) {
        return true;
    }
    // Signal is the scalar supertype — compatible with all concrete scalar types.
    if (source.port_type == PortType::Signal || target.port_type == PortType::Signal) {
        return true;
    }
    return source.port_type == target.port_type;
}

} // namespace bp2
