#pragma once

#include "port_descriptor.h"
#include <vector>
#include <unordered_map>
#include <optional>

namespace bp2 {

class Interface {
public:
    Interface() = default;
    explicit Interface(std::vector<PortDescriptor> ports);

    size_t size() const { return ports_.size(); }
    bool empty() const { return ports_.empty(); }

    auto begin() const { return ports_.begin(); }
    auto end() const { return ports_.end(); }

    std::vector<PortDescriptor> const& ports() const { return ports_; }

    std::optional<PortDescriptor> find(ui::InternedId name) const;
    bool has(ui::InternedId name) const;
    PortDescriptor const& at(ui::InternedId name) const;

    bool operator==(Interface const& o) const { return ports_ == o.ports_; }
    bool operator!=(Interface const& o) const { return !(*this == o); }

private:
    std::vector<PortDescriptor> ports_;
    std::unordered_map<ui::InternedId, size_t> name_to_idx_;
};

} // namespace bp2
