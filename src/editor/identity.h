#pragma once

#include <string>

namespace editor {

/// Typed wrapper for node identity strings.
/// Prevents accidental implicit construction from raw std::string —
/// callers must go through the explicit factory NodeId::from_string().
class NodeId {
public:
    static NodeId from_string(std::string v) {
        return NodeId(std::move(v));
    }

    /// Default-constructed NodeId is empty (sentinel).
    NodeId() = default;

    bool empty() const { return value_.empty(); }
    const std::string& str() const { return value_; }

    bool operator==(const NodeId& other) const { return value_ == other.value_; }
    bool operator!=(const NodeId& other) const { return value_ != other.value_; }

private:
    std::string value_;

    explicit NodeId(std::string v) : value_(std::move(v)) {}
};

} // namespace editor
