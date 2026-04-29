#include "focus_scope.h"
#include "editor/document.h"
#include "editor/window/blueprint_window.h"

bool FocusScope::is_root() const {
    return is_valid() && window->resolved_scope_id().is_root();
}

bool FocusScope::is_subwindow() const {
    return is_valid() && !window->resolved_scope_id().is_root();
}

bool FocusScope::is_valid() const {
    return document != nullptr && window != nullptr;
}

bool FocusScope::is_read_only() const {
    return window && window->read_only;
}
