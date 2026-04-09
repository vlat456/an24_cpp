#include "blueprint.h"
#include "blueprint_v2/validation/invariant_checker.h"
#include "ui/core/interned_id.h"
#include <algorithm>

namespace bp2 {

Blueprint::Nested::Embedded::Embedded(const Embedded& other)
    : blueprint_id(other.blueprint_id) {
    if (other.inline_def) {
        inline_def = std::make_unique<Blueprint>(*other.inline_def);
    }
}

Blueprint::Nested::Embedded& Blueprint::Nested::Embedded::operator=(const Embedded& other) {
    if (this != &other) {
        blueprint_id = other.blueprint_id;
        inline_def.reset();
        if (other.inline_def) {
            inline_def = std::make_unique<Blueprint>(*other.inline_def);
        }
    }
    return *this;
}

Blueprint::Nested::Nested(const Blueprint::Nested& other)
    : id(other.id)
    , x(other.x)
    , y(other.y)
    , content_(other.content_) {
}

Blueprint::Nested& Blueprint::Nested::operator=(const Blueprint::Nested& other) {
    if (this != &other) {
        id = other.id;
        x = other.x;
        y = other.y;
        content_ = other.content_;
    }
    return *this;
}

Blueprint::Blueprint(Blueprint const& other)
    : id_(other.id_)
    , display_name_(other.display_name_)
    , iface_(other.iface_)
    , pan_x_(other.pan_x_)
    , pan_y_(other.pan_y_)
    , zoom_(other.zoom_)
    , grid_step_(other.grid_step_)
    , name_(other.name_)
    , node_idx_valid_(false)
    , wire_idx_valid_(false)
    , nested_idx_valid_(false) {
    for (auto const& n : other.nodes_) {
        nodes_.push_back(n);  // Node is copyable
    }
    for (auto const& w : other.wires_) {
        wires_.push_back(w);  // Wire is copyable
    }
    for (auto const& n : other.nested_) nested_.push_back(n);
}

Blueprint::Blueprint(Blueprint&& other) noexcept
    : id_(other.id_)
    , display_name_(std::move(other.display_name_))
    , iface_(std::move(other.iface_))
    , nodes_(std::move(other.nodes_))
    , wires_(std::move(other.wires_))
    , nested_(std::move(other.nested_))
    , pan_x_(other.pan_x_)
    , pan_y_(other.pan_y_)
    , zoom_(other.zoom_)
    , grid_step_(other.grid_step_)
    , name_(std::move(other.name_))
    , node_idx_valid_(false)
    , wire_idx_valid_(false)
    , nested_idx_valid_(false) {
}

Blueprint& Blueprint::operator=(Blueprint const& other) {
    if (this != &other) {
        id_ = other.id_;
        display_name_ = other.display_name_;
        iface_ = other.iface_;
        nodes_.clear();
        for (auto const& n : other.nodes_) nodes_.push_back(n);
        wires_.clear();
        for (auto const& w : other.wires_) wires_.push_back(w);
        nested_.clear();
        for (auto const& n : other.nested_) nested_.push_back(n);
        pan_x_ = other.pan_x_;
        pan_y_ = other.pan_y_;
        zoom_ = other.zoom_;
        grid_step_ = other.grid_step_;
        name_ = other.name_;
        node_idx_valid_ = false;
        wire_idx_valid_ = false;
        nested_idx_valid_ = false;
    }
    return *this;
}

Blueprint& Blueprint::operator=(Blueprint&& other) noexcept {
    if (this != &other) {
        id_ = other.id_;
        display_name_ = std::move(other.display_name_);
        iface_ = std::move(other.iface_);
        nodes_ = std::move(other.nodes_);
        wires_ = std::move(other.wires_);
        nested_ = std::move(other.nested_);
        pan_x_ = other.pan_x_;
        pan_y_ = other.pan_y_;
        zoom_ = other.zoom_;
        grid_step_ = other.grid_step_;
        name_ = std::move(other.name_);
        node_idx_valid_ = false;
        wire_idx_valid_ = false;
        nested_idx_valid_ = false;
    }
    return *this;
}

void Blueprint::ensure_node_index() const {
    if (node_idx_valid_) return;
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
    if (it == node_idx_.end()) return nullptr;
    return &nodes_[it->second];
}

Blueprint Blueprint::with_node(Node n) const {
    Blueprint copy = *this;
    copy.nodes_.push_back(std::move(n));
    copy.node_idx_valid_ = false;
    return copy;
}

Blueprint Blueprint::without_node(ui::InternedId id) const {
    Blueprint copy = *this;
    copy.nodes_.erase(
        std::remove_if(copy.nodes_.begin(), copy.nodes_.end(),
            [id](Node const& n) { return n.semantic.id == id; }),
        copy.nodes_.end()
    );
    copy.node_idx_valid_ = false;
    return copy;
}

void Blueprint::ensure_wire_index() const {
    if (wire_idx_valid_) return;
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
    if (it == wire_idx_.end()) return nullptr;
    return &wires_[it->second];
}

Blueprint Blueprint::with_wire(Wire w) const {
    Blueprint copy = *this;
    copy.wires_.push_back(std::move(w));
    copy.wire_idx_valid_ = false;
    return copy;
}

Blueprint Blueprint::without_wire(ui::InternedId id) const {
    Blueprint copy = *this;
    copy.wires_.erase(
        std::remove_if(copy.wires_.begin(), copy.wires_.end(),
            [id](Wire const& w) { return w.id == id; }),
        copy.wires_.end()
    );
    copy.wire_idx_valid_ = false;
    return copy;
}

void Blueprint::ensure_nested_index() const {
    if (nested_idx_valid_) return;
    nested_idx_.clear();
    nested_idx_.reserve(nested_.size());
    for (size_t i = 0; i < nested_.size(); ++i) {
        nested_idx_[nested_[i].id] = i;
    }
    nested_idx_valid_ = true;
}

Blueprint::Nested const* Blueprint::find_nested(ui::InternedId id) const {
    ensure_nested_index();
    auto it = nested_idx_.find(id);
    if (it == nested_idx_.end()) return nullptr;
    return &nested_[it->second];
}

Blueprint::Nested const* Blueprint::find_hosted_nested(Node const& node) const {
    return find_nested(node.semantic.id);
}

Blueprint::Node const* Blueprint::find_host_node(Nested const& nested) const {
    return find_node(nested.id);
}

bool Blueprint::is_embedded_proxy_node(Node const& node) const {
    const Nested* nested = find_hosted_nested(node);
    return node.view.expandable && nested && nested->is_embedded();
}

Interface const& Blueprint::effective_node_iface(ui::InternedId node_id) const {
    const Node* node = find_node(node_id);
    if (!node) {
        throw std::logic_error("Blueprint::effective_node_iface: node not found");
    }
    return effective_node_iface(*node);
}

Interface const& Blueprint::effective_node_iface(Node const& node) const {
    const Nested* nested = find_hosted_nested(node);
    if (nested) {
        return nested->resolved_iface();
    }
    return node.semantic.iface;
}

Blueprint Blueprint::with_nested(Nested n) const {
    Blueprint copy = *this;
    copy.nested_.push_back(std::move(n));
    copy.nested_idx_valid_ = false;
    return copy;
}

Blueprint Blueprint::without_nested(ui::InternedId id) const {
    Blueprint copy = *this;
    copy.nested_.erase(
        std::remove_if(copy.nested_.begin(), copy.nested_.end(),
            [id](Nested const& n) { return n.id == id; }),
        copy.nested_.end()
    );
    copy.nested_idx_valid_ = false;
    return copy;
}

Blueprint Blueprint::with_id(ui::InternedId id) const {
    Blueprint copy = *this;
    copy.id_ = id;
    return copy;
}

Blueprint Blueprint::with_display_name(std::string name) const {
    Blueprint copy = *this;
    copy.display_name_ = std::move(name);
    return copy;
}

Blueprint Blueprint::with_interface(Interface iface) const {
    Blueprint copy = *this;
    copy.iface_ = std::move(iface);
    return copy;
}

Blueprint Blueprint::with_viewport(float px, float py, float zoom, float grid_step) const {
    Blueprint copy = *this;
    copy.pan_x_ = px;
    copy.pan_y_ = py;
    copy.zoom_ = zoom;
    copy.grid_step_ = grid_step;
    return copy;
}

Blueprint Blueprint::with_name(std::string n) const {
    Blueprint copy = *this;
    copy.name_ = std::move(n);
    return copy;
}

bool Blueprint::nested_equals(Nested const& a, Nested const& b) {
    if (a.id != b.id) return false;
    if (a.x != b.x) return false;
    if (a.y != b.y) return false;

    if (a.is_embedded() != b.is_embedded()) return false;
    if (a.is_embedded()) {
        if (a.blueprint_id() != b.blueprint_id()) return false;
        bool a_has = (a.inline_def() != nullptr);
        bool b_has = (b.inline_def() != nullptr);
        if (a_has != b_has) return false;
        if (a_has && b_has && *a.inline_def() != *b.inline_def()) return false;
    } else {
        if (a.blueprint_id() != b.blueprint_id()) return false;
        if (a.resolved_iface() != b.resolved_iface()) return false;
    }

    return true;
}

bool Blueprint::operator==(Blueprint const& other) const {
    if (id_ != other.id_) return false;
    if (display_name_ != other.display_name_) return false;
    if (name_ != other.name_) return false;
    if (iface_ != other.iface_) return false;
    if (pan_x_ != other.pan_x_) return false;
    if (pan_y_ != other.pan_y_) return false;
    if (zoom_ != other.zoom_) return false;
    if (grid_step_ != other.grid_step_) return false;
    if (nodes_ != other.nodes_) return false;
    if (wires_ != other.wires_) return false;
    if (nested_.size() != other.nested_.size()) return false;
    for (size_t i = 0; i < nested_.size(); ++i) {
        if (!nested_equals(nested_[i], other.nested_[i])) return false;
    }
    return true;
}


Blueprint Blueprint::clone(ui::InternedId new_id) const {
    Blueprint copy = *this;
    copy.id_ = new_id;
    copy.display_name_ = std::string("Copy of ") + display_name_;
    return copy;
}

std::vector<std::pair<Path, PortDescriptor>> Blueprint::all_ports(PathArena& arena) const {
    std::vector<std::pair<Path, PortDescriptor>> result;
    collect_ports_recursive(result, arena, arena.root());
    return result;
}

void Blueprint::validate(::TypeRegistry const& parser_registry, ui::StringInterner& interner) const {
    PathArena arena(interner);
    auto r = InvariantChecker::validate(*this, arena, parser_registry, interner);
    if (!r.valid) {
        throw std::runtime_error(std::string("Blueprint validation: ") + r.error);
    }
}

void Blueprint::validate(::TypeRegistry const& parser_registry,
                        ui::StringInterner& interner,
                        PathArena const& arena) const {
    auto r = InvariantChecker::validate(*this, arena, parser_registry, interner);
    if (!r.valid) {
        throw std::runtime_error(std::string("Blueprint validation: ") + r.error);
    }
}

void Blueprint::collect_ports_recursive(
    std::vector<std::pair<Path, PortDescriptor>>& result,
    PathArena& arena,
    Path prefix) const {
    // Interface ports of this blueprint (boundary ports)
    for (auto const& port : iface_.ports()) {
        Path port_path = arena.make_port(prefix, port.name);
        result.push_back({port_path, port});
    }
    // Node ports
    for (auto const& node : nodes_) {
        Path node_path = arena.make_node(prefix, node.semantic.id);
        for (auto const& port : effective_node_iface(node)) {
            Path port_path = arena.make_port(node_path, port.name);
            result.push_back({port_path, port});
        }
    }
    // Nested instance ports (recursively)
    for (auto const& nested : nested_) {
        Path nested_path = arena.make_nested(prefix, nested.id);
        if (auto* def = nested.inline_def()) {
            def->collect_ports_recursive(result, arena, nested_path);
        } else {
            for (auto const& port : nested.resolved_iface()) {
                Path port_path = arena.make_port(nested_path, port.name);
                result.push_back({port_path, port});
            }
        }
    }
}

} // namespace bp2
