#pragma once

/// Hash for string_view in unordered containers.
/// Shared by visual rendering and simulation bridge — extracted here
/// to avoid pulling visual headers into simulation headers.

#include <string_view>
#include <cstddef>

namespace visual {

struct StringViewHash {
    using is_transparent = void;
    size_t operator()(std::string_view sv) const noexcept {
        return std::hash<std::string_view>{}(sv);
    }
};

} // namespace visual
