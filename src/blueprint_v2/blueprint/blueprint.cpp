#include "blueprint.h"
#include "blueprint_v2/registry/type_registry.h"
#include "ui/core/interned_id.h"
#include <algorithm>

namespace bp2 {

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
    for (auto const& n : other.nested_) {
        Nested copy;
        copy.id = n.id;
        copy.blueprint_id = n.blueprint_id;
        copy.embedded = n.embedded;
        copy.iface = n.iface;
        copy.x = n.x;
        copy.y = n.y;
        if (n.inline_def) {
            copy.inline_def = std::make_unique<Blueprint>(*n.inline_def);
        }
        nested_.push_back(std::move(copy));
    }
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
        for (auto const& n : other.nested_) {
            Nested copy;
            copy.id = n.id;
            copy.blueprint_id = n.blueprint_id;
            copy.embedded = n.embedded;
            copy.iface = n.iface;
            copy.x = n.x;
            copy.y = n.y;
            if (n.inline_def) {
                copy.inline_def = std::make_unique<Blueprint>(*n.inline_def);
            }
            nested_.push_back(std::move(copy));
        }
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
        node_idx_[nodes_[i].id] = i;
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
            [id](Node const& n) { return n.id == id; }),
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
    if (a.blueprint_id != b.blueprint_id) return false;
    if (a.embedded != b.embedded) return false;
    if (a.iface != b.iface) return false;
    if (a.x != b.x) return false;
    if (a.y != b.y) return false;
    
    bool a_has = (a.inline_def != nullptr);
    bool b_has = (b.inline_def != nullptr);
    if (a_has != b_has) return false;
    
    if (a_has && b_has && *a.inline_def != *b.inline_def) return false;
    
    return true;
}

bool Blueprint::operator==(Blueprint const& other) const {
    if (id_ != other.id_) return false;
    if (display_name_ != other.display_name_) return false;
    if (iface_ != other.iface_) return false;
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

void Blueprint::validate(TypeRegistry const& registry) const {
    // I1: Uniqueness -- node IDs
    for (size_t i = 0; i < nodes_.size(); ++i) {
        for (size_t j = i + 1; j < nodes_.size(); ++j) {
            if (nodes_[i].id == nodes_[j].id) {
                throw std::runtime_error(
                    "Blueprint validation: duplicate node ID");
            }
        }
    }
    // I1: wire IDs
    for (size_t i = 0; i < wires_.size(); ++i) {
        for (size_t j = i + 1; j < wires_.size(); ++j) {
            if (wires_[i].id == wires_[j].id) {
                throw std::runtime_error(
                    "Blueprint validation: duplicate wire ID");
            }
        }
    }
    // I1: nested IDs
    for (size_t i = 0; i < nested_.size(); ++i) {
        for (size_t j = i + 1; j < nested_.size(); ++j) {
            if (nested_[i].id == nested_[j].id) {
                throw std::runtime_error(
                    "Blueprint validation: duplicate nested ID");
            }
        }
    }
    // I2: node types exist in registry
    for (auto const& node : nodes_) {
        if (!registry.has(node.type)) {
            throw std::runtime_error(
                "Blueprint validation: unknown node type");
        }
    }
    // I5: embedded consistency + I2 reference validity
    for (auto const& n : nested_) {
        if (n.embedded && !n.inline_def) {
            throw std::runtime_error(
                "Blueprint validation: embedded nested missing inline_def");
        }
        if (!n.embedded && n.blueprint_id.empty()) {
            throw std::runtime_error(
                "Blueprint validation: non-embedded nested missing blueprint_id");
        }
        if (!n.embedded && !n.blueprint_id.empty() && !registry.has(n.blueprint_id)) {
            throw std::runtime_error(
                "Blueprint validation: unknown nested blueprint");
        }
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
        Path node_path = arena.make_node(prefix, node.id);
        for (auto const& port : node.iface) {
            Path port_path = arena.make_port(node_path, port.name);
            result.push_back({port_path, port});
        }
    }
    // Nested instance ports (recursively)
    for (auto const& nested : nested_) {
        Path nested_path = arena.make_nested(prefix, nested.id);
        if (nested.embedded && nested.inline_def) {
            nested.inline_def->collect_ports_recursive(result, arena, nested_path);
        } else {
            // For reference-mode, expose the cached interface ports
            for (auto const& port : nested.iface) {
                Path port_path = arena.make_port(nested_path, port.name);
                result.push_back({port_path, port});
            }
        }
    }
}

} // namespace bp2
