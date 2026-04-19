#include "blueprint.h"

#include "blueprint_v2/interface/type_definition_interface.h"
#include "blueprint_v2/validation/invariant_checker.h"

#include <algorithm>

namespace bp2 {

Blueprint::Node::BlueprintSource::Embedded::Embedded(const Embedded& other) {
    if (other.blueprint) {
        blueprint = std::make_unique<Blueprint>(*other.blueprint);
    }
}

Blueprint::Node::BlueprintSource::Embedded&
Blueprint::Node::BlueprintSource::Embedded::operator=(const Embedded& other) {
    if (this != &other) {
        blueprint.reset();
        if (other.blueprint) {
            blueprint = std::make_unique<Blueprint>(*other.blueprint);
        }
    }
    return *this;
}

bool Blueprint::Node::BlueprintSource::Embedded::operator==(Embedded const& o) const {
    if (!blueprint && !o.blueprint) return true;
    if (!blueprint || !o.blueprint) return false;
    return *blueprint == *o.blueprint;
}

Blueprint::Node::BlueprintSource::BlueprintSource(const BlueprintSource& other)
    : value(other.value) {
}

Blueprint::Node::BlueprintSource&
Blueprint::Node::BlueprintSource::operator=(const BlueprintSource& other) {
    if (this != &other) {
        value = other.value;
    }
    return *this;
}

Blueprint::Node::BlueprintSource
Blueprint::Node::BlueprintSource::make_embedded(std::unique_ptr<Blueprint> blueprint) {
    if (!blueprint) {
        throw std::logic_error("BlueprintSource::make_embedded requires non-null blueprint");
    }
    if (blueprint->id().empty()) {
        throw std::logic_error("BlueprintSource::make_embedded requires blueprint with non-empty id");
    }
    return BlueprintSource(Embedded{std::move(blueprint)});
}

Blueprint::Node::BlueprintSource
Blueprint::Node::BlueprintSource::make_reference(ui::InternedId blueprint_id) {
    if (blueprint_id.empty()) {
        throw std::logic_error("BlueprintSource::make_reference requires non-empty blueprint_id");
    }
    return BlueprintSource(Reference{blueprint_id});
}

bool Blueprint::Node::BlueprintSource::is_embedded() const {
    return std::holds_alternative<Embedded>(value);
}

bool Blueprint::Node::BlueprintSource::is_reference() const {
    return std::holds_alternative<Reference>(value);
}

ui::InternedId Blueprint::Node::BlueprintSource::blueprint_id() const {
    if (auto* embedded = std::get_if<Embedded>(&value)) {
        return embedded->blueprint->id();
    }
    return std::get<Reference>(value).blueprint_id;
}

Blueprint const* Blueprint::Node::BlueprintSource::inline_def() const {
    if (auto* embedded = std::get_if<Embedded>(&value)) {
        return embedded->blueprint.get();
    }
    return nullptr;
}

Blueprint* Blueprint::Node::BlueprintSource::inline_def_mut() {
    if (auto* embedded = std::get_if<Embedded>(&value)) {
        return embedded->blueprint.get();
    }
    return nullptr;
}

void Blueprint::Node::BlueprintSource::set_inline_def(std::unique_ptr<Blueprint> blueprint) {
    if (!blueprint) {
        throw std::logic_error("BlueprintSource::set_inline_def requires non-null blueprint");
    }
    if (blueprint->id().empty()) {
        throw std::logic_error("BlueprintSource::set_inline_def requires blueprint with non-empty id");
    }
    auto* embedded = std::get_if<Embedded>(&value);
    if (!embedded) {
        throw std::logic_error("BlueprintSource::set_inline_def requires embedded source");
    }
    embedded->blueprint = std::move(blueprint);
}

bool Blueprint::Node::BlueprintSource::canonical_eq(const BlueprintSource& other) const {
    if (is_embedded() != other.is_embedded()) {
        return false;
    }
    if (blueprint_id() != other.blueprint_id()) {
        return false;
    }
    if (is_embedded()) {
        const Blueprint* lhs = inline_def();
        const Blueprint* rhs = other.inline_def();
        if ((lhs == nullptr) != (rhs == nullptr)) {
            return false;
        }
        return lhs == nullptr || lhs->canonical_eq(*rhs);
    }
    return true;
}

bool Blueprint::Node::BlueprintSource::operator==(const BlueprintSource& other) const {
    if (is_embedded() != other.is_embedded()) {
        return false;
    }
    if (blueprint_id() != other.blueprint_id()) {
        return false;
    }
    if (is_embedded()) {
        const Blueprint* lhs = inline_def();
        const Blueprint* rhs = other.inline_def();
        if ((lhs == nullptr) != (rhs == nullptr)) {
            return false;
        }
        return lhs == nullptr || *lhs == *rhs;
    }
    return true;
}

Blueprint::Blueprint(Blueprint const& other)
    : id_(other.id_)
    , name_(other.name_)
    , iface_(other.iface_)
    , node_idx_valid_(false)
    , wire_idx_valid_(false) {
    for (auto const& node : other.nodes_) {
        nodes_.push_back(node);
    }
    for (auto const& wire : other.wires_) {
        wires_.push_back(wire);
    }
}

Blueprint::Blueprint(Blueprint&& other) noexcept
    : id_(other.id_)
    , name_(std::move(other.name_))
    , iface_(std::move(other.iface_))
    , nodes_(std::move(other.nodes_))
    , wires_(std::move(other.wires_))
    , node_idx_valid_(false)
    , wire_idx_valid_(false) {
}

Blueprint& Blueprint::operator=(Blueprint const& other) {
    if (this != &other) {
        id_ = other.id_;
        name_ = other.name_;
        iface_ = other.iface_;
        nodes_.clear();
        for (auto const& node : other.nodes_) {
            nodes_.push_back(node);
        }
        wires_.clear();
        for (auto const& wire : other.wires_) {
            wires_.push_back(wire);
        }
        node_idx_valid_ = false;
        wire_idx_valid_ = false;
    }
    return *this;
}

Blueprint& Blueprint::operator=(Blueprint&& other) noexcept {
    if (this != &other) {
        id_ = other.id_;
        name_ = std::move(other.name_);
        iface_ = std::move(other.iface_);
        nodes_ = std::move(other.nodes_);
        wires_ = std::move(other.wires_);
        node_idx_valid_ = false;
        wire_idx_valid_ = false;
    }
    return *this;
}

void Blueprint::ensure_node_index() const {
    if (node_idx_valid_) {
        return;
    }
    node_idx_.clear();
    node_idx_.reserve(nodes_.size());
    for (size_t i = 0; i < nodes_.size(); ++i) {
        node_idx_[nodes_[i].semantic.id] = i;
    }
    node_idx_valid_ = true;
}

Blueprint::Node const* Blueprint::find_node(ui::InternedId id) const {
    ensure_node_index();
    auto it = node_idx_.find(id);
    if (it == node_idx_.end()) {
        return nullptr;
    }
    return &nodes_[it->second];
}

Blueprint::Node const* Blueprint::find_blueprint_instance(ui::InternedId id) const {
    const auto* node = find_node(id);
    if (!node || !node->is_blueprint_instance()) {
        return nullptr;
    }
    return node;
}

bool Blueprint::is_blueprint_instance_node(Node const& node) const {
    return node.is_blueprint_instance();
}

bool Blueprint::is_embedded_blueprint_instance(Node const& node) const {
    return node.has_embedded_blueprint();
}

bool Blueprint::is_referenced_blueprint_instance(Node const& node) const {
    return node.has_referenced_blueprint();
}

bool Blueprint::Node::canonical_eq(Node const& o) const {
    if (content.index() != o.content.index()) {
        return false;
    }

    bool content_equal = false;
    if (is_component()) {
        content_equal = component() == o.component();
    } else if (is_blueprint_instance()) {
        content_equal = blueprint_instance().source.canonical_eq(o.blueprint_instance().source);
    } else {
        content_equal = bridge_port() == o.bridge_port();
    }

    return semantic == o.semantic && content_equal && layout == o.layout && view.canonical_eq(o.view);
}

Blueprint Blueprint::with_node(Node node) const {
    Blueprint copy = *this;
    copy.nodes_.push_back(std::move(node));
    copy.node_idx_valid_ = false;
    return copy;
}

Blueprint Blueprint::without_node(ui::InternedId id) const {
    Blueprint copy = *this;
    copy.nodes_.erase(
        std::remove_if(copy.nodes_.begin(), copy.nodes_.end(),
            [id](Node const& node) { return node.semantic.id == id; }),
        copy.nodes_.end());
    copy.node_idx_valid_ = false;
    return copy;
}

void Blueprint::ensure_wire_index() const {
    if (wire_idx_valid_) {
        return;
    }
    wire_idx_.clear();
    wire_idx_.reserve(wires_.size());
    for (size_t i = 0; i < wires_.size(); ++i) {
        wire_idx_[wires_[i].id] = i;
    }
    wire_idx_valid_ = true;
}

Blueprint::Wire const* Blueprint::find_wire(ui::InternedId id) const {
    ensure_wire_index();
    auto it = wire_idx_.find(id);
    if (it == wire_idx_.end()) {
        return nullptr;
    }
    return &wires_[it->second];
}

Blueprint Blueprint::with_wire(Wire wire) const {
    Blueprint copy = *this;
    copy.wires_.push_back(std::move(wire));
    copy.wire_idx_valid_ = false;
    return copy;
}

Blueprint Blueprint::without_wire(ui::InternedId id) const {
    Blueprint copy = *this;
    copy.wires_.erase(
        std::remove_if(copy.wires_.begin(), copy.wires_.end(),
            [id](Wire const& wire) { return wire.id == id; }),
        copy.wires_.end());
    copy.wire_idx_valid_ = false;
    return copy;
}

Interface const& Blueprint::effective_node_iface(ui::InternedId node_id) const {
    const Node* node = find_node(node_id);
    if (!node) {
        throw std::logic_error("Blueprint::effective_node_iface: node not found");
    }
    return effective_node_iface(*node);
}

Interface const& Blueprint::effective_node_iface(Node const& node) const {
    if (node.is_blueprint_instance()) {
        const Blueprint* embedded = node.blueprint_instance().source.inline_def();
        if (!embedded) {
            throw std::logic_error("Blueprint::effective_node_iface requires explicit authority for referenced blueprint instances");
        }
        return embedded->iface();
    }
    if (node.is_bridge_port()) {
        return node.bridge_port().iface;
    }
    return node.component().iface;
}

Interface Blueprint::effective_node_iface(ui::InternedId node_id,
                                          ::ComponentRegistry const& parser_registry,
                                          ui::StringInterner& interner) const {
    const Node* node = find_node(node_id);
    if (!node) {
        throw std::logic_error("Blueprint::effective_node_iface: node not found");
    }
    return effective_node_iface(*node, parser_registry, interner);
}

Interface Blueprint::effective_node_iface(Node const& node,
                                          ::ComponentRegistry const& parser_registry,
                                          ui::StringInterner& interner) const {
    if (!node.is_blueprint_instance()) {
        return effective_node_iface(node);
    }

    if (const Blueprint* embedded = node.blueprint_instance().source.inline_def()) {
        return embedded->iface();
    }

    const auto* def = parser_registry.get(std::string(interner.resolve(node.blueprint_instance().source.blueprint_id())));
    if (!def) {
        throw std::logic_error("Blueprint::effective_node_iface: unknown referenced blueprint_id");
    }
    return interface_from_type_definition(*def, interner);
}

Blueprint Blueprint::with_id(ui::InternedId id) const {
    Blueprint copy = *this;
    copy.id_ = id;
    return copy;
}

Blueprint Blueprint::with_name(std::string name) const {
    Blueprint copy = *this;
    copy.name_ = std::move(name);
    return copy;
}

Blueprint Blueprint::with_interface(Interface iface) const {
    Blueprint copy = *this;
    copy.iface_ = std::move(iface);
    return copy;
}

bool Blueprint::operator==(Blueprint const& other) const {
    return id_ == other.id_
        && name_ == other.name_
        && iface_ == other.iface_
        && nodes_ == other.nodes_
        && wires_ == other.wires_;
}

bool Blueprint::canonical_eq(Blueprint const& other) const {
    if (id_ != other.id_ || name_ != other.name_ || iface_ != other.iface_ || wires_ != other.wires_) {
        return false;
    }
    if (nodes_.size() != other.nodes_.size()) {
        return false;
    }
    for (size_t i = 0; i < nodes_.size(); ++i) {
        if (!nodes_[i].canonical_eq(other.nodes_[i])) {
            return false;
        }
    }
    return true;
}

Blueprint Blueprint::clone(ui::InternedId new_id) const {
    Blueprint copy = *this;
    copy.id_ = new_id;
    copy.name_ = std::string("Copy of ") + name_;
    return copy;
}

std::vector<std::pair<Path, PortDescriptor>> Blueprint::all_ports(PathArena& arena) const {
    std::vector<std::pair<Path, PortDescriptor>> result;
    collect_ports_recursive(result, arena, arena.root());
    return result;
}

std::vector<std::pair<Path, PortDescriptor>> Blueprint::all_ports(PathArena& arena,
                                                                  ::ComponentRegistry const& parser_registry,
                                                                  ui::StringInterner& interner) const {
    std::vector<std::pair<Path, PortDescriptor>> result;
    collect_ports_recursive(result, arena, arena.root(), parser_registry, interner);
    return result;
}

void Blueprint::validate(::ComponentRegistry const& parser_registry, ui::StringInterner& interner) const {
    PathArena arena(interner);
    auto result = InvariantChecker::validate(*this, arena, parser_registry, interner);
    if (!result.valid) {
        throw std::runtime_error(std::string("Blueprint validation: ") + result.error);
    }
}

void Blueprint::validate(::ComponentRegistry const& parser_registry,
                         ui::StringInterner& interner,
                         PathArena const& arena) const {
    auto result = InvariantChecker::validate(*this, arena, parser_registry, interner);
    if (!result.valid) {
        throw std::runtime_error(std::string("Blueprint validation: ") + result.error);
    }
}

void Blueprint::collect_ports_recursive(
    std::vector<std::pair<Path, PortDescriptor>>& result,
    PathArena& arena,
    Path prefix) const {
    for (auto const& port : iface_.ports()) {
        result.push_back({arena.make_port(prefix, port.name), port});
    }

    for (auto const& node : nodes_) {
        Path node_path = arena.make_node(prefix, node.semantic.id);
        for (auto const& port : effective_node_iface(node)) {
            result.push_back({arena.make_port(node_path, port.name), port});
        }

        if (!is_embedded_blueprint_instance(node)) {
            continue;
        }

        const Blueprint* inline_bp = node.blueprint_instance().source.inline_def();
        if (!inline_bp) {
            throw std::logic_error("Blueprint::collect_ports_recursive: embedded blueprint instance missing inline blueprint");
        }
        inline_bp->collect_ports_recursive(result, arena, arena.make_nested(prefix, node.semantic.id));
    }
}

void Blueprint::collect_ports_recursive(
    std::vector<std::pair<Path, PortDescriptor>>& result,
    PathArena& arena,
    Path prefix,
    ::ComponentRegistry const& parser_registry,
    ui::StringInterner& interner) const {
    for (auto const& port : iface_.ports()) {
        result.push_back({arena.make_port(prefix, port.name), port});
    }

    for (auto const& node : nodes_) {
        Path node_path = arena.make_node(prefix, node.semantic.id);
        for (auto const& port : effective_node_iface(node, parser_registry, interner)) {
            result.push_back({arena.make_port(node_path, port.name), port});
        }

        if (!is_embedded_blueprint_instance(node)) {
            continue;
        }

        const Blueprint* inline_bp = node.blueprint_instance().source.inline_def();
        if (!inline_bp) {
            throw std::logic_error("Blueprint::collect_ports_recursive: embedded blueprint instance missing inline blueprint");
        }
        inline_bp->collect_ports_recursive(result, arena, arena.make_nested(prefix, node.semantic.id), parser_registry, interner);
    }
}

} // namespace bp2
