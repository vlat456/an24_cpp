#include "interface.h"

namespace bp2 {

Interface::Interface(std::vector<PortDescriptor> ports)
    : ports_(std::move(ports)) {
    name_to_idx_.reserve(ports_.size());
    for (size_t i = 0; i < ports_.size(); ++i) {
        name_to_idx_[ports_[i].name] = i;
    }
}

std::optional<PortDescriptor> Interface::find(ui::InternedId name) const {
    auto it = name_to_idx_.find(name);
    if (it == name_to_idx_.end()) return std::nullopt;
    return ports_[it->second];
}

bool Interface::has(ui::InternedId name) const {
    return name_to_idx_.count(name) > 0;
}

PortDescriptor const& Interface::at(ui::InternedId name) const {
    auto it = name_to_idx_.find(name);
    return ports_[it->second];
}

} // namespace bp2
