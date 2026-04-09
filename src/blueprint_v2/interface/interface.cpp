#include "interface.h"

#include <algorithm>
#include <stdexcept>

namespace bp2 {

Interface::Interface(std::vector<PortDescriptor> ports)
    : ports_(std::move(ports)) {
    std::sort(ports_.begin(), ports_.end(), [](const PortDescriptor& a, const PortDescriptor& b) {
        if (a.name != b.name) {
            return a.name.raw() < b.name.raw();
        }
        if (a.domain != b.domain) {
            return static_cast<int>(a.domain) < static_cast<int>(b.domain);
        }
        if (a.direction != b.direction) {
            return static_cast<int>(a.direction) < static_cast<int>(b.direction);
        }
        return static_cast<int>(a.port_type) < static_cast<int>(b.port_type);
    });

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
    if (it == name_to_idx_.end()) {
        throw std::out_of_range("Interface::at: port name not found");
    }
    return ports_[it->second];
}

} // namespace bp2
