#pragma once

#include <string>

namespace editor {

/// Typed wrapper for document identity strings.
///
/// Cross-Document identity: survives the InternedId Purity Law because
/// OscilloscopeModel is global and indexes by DocumentId. Strings are the
/// correct wire format for cross-Document boundaries.
class DocumentId {
public:
    static DocumentId from_string(std::string v) {
        return DocumentId(std::move(v));
    }

    DocumentId() = default;

    bool empty() const { return value_.empty(); }
    const std::string& str() const { return value_; }

    bool operator==(const DocumentId& other) const { return value_ == other.value_; }
    bool operator!=(const DocumentId& other) const { return value_ != other.value_; }

    // std::map / std::unordered_map support
    bool operator<(const DocumentId& other) const { return value_ < other.value_; }

private:
    std::string value_;

    explicit DocumentId(std::string v) : value_(std::move(v)) {}
};

} // namespace editor

template <>
struct std::hash<editor::DocumentId> {
    size_t operator()(const editor::DocumentId& id) const noexcept {
        return std::hash<std::string>{}(id.str());
    }
};
