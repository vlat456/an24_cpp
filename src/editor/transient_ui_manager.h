#pragma once

#include "editor/identity.h"
#include <vector>

class WindowSystem;

/// Type-erased registry for transient UI element lifecycle.
///
/// Uses function-pointer type erasure (same technique as ErasedStep):
/// void* + fn-ptr pairs, no virtual, no heap allocation in per-frame path.
///
/// Each registered entry provides 4 operations via template deduction:
///   is_open()          — is this element currently showing?
///   close()            — reset to closed state
///   owns_document(id)  — is this element bound to the given document?
///   still_valid(ws)    — is the element's owner still alive?
///
/// Registration happens once (in WindowSystem constructor).
/// Lifecycle methods iterate the entry vector.
class TransientUIManager {
public:
    /// Register a transient UI element.
    /// T must provide: is_open(), close(), owns_document(const DocumentId&),
    ///                 still_valid(WindowSystem&)
    template<typename T>
    void register_entry(T& popup) {
        entries_.push_back(Entry{
            &popup,
            [](const void* p) -> bool { return static_cast<const T*>(p)->is_open(); },
            [](void* p) { static_cast<T*>(p)->close(); },
            [](const void* p, const editor::DocumentId& id) -> bool {
                return static_cast<const T*>(p)->owns_document(id);
            },
            [](const void* p, WindowSystem& ws) -> bool {
                return static_cast<const T*>(p)->still_valid(ws);
            }
        });
    }

    /// Close all entries bound to a specific document (regardless of is_open).
    void close_for_document(const editor::DocumentId& id);

    /// Close all entries unconditionally.
    void close_all();

    /// Reconcile: close open entries whose owner is no longer valid.
    void reconcile(WindowSystem& ws);

private:
    struct Entry {
        void* self;
        bool (*is_open)(const void*);
        void (*close)(void*);
        bool (*owns_document)(const void*, const editor::DocumentId&);
        bool (*still_valid)(const void*, WindowSystem&);
    };

    std::vector<Entry> entries_;
};
