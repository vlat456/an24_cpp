#include "focus_scope.h"
#include "editor/document.h"
#include "editor/window/blueprint_window.h"

bool FocusScope::Resolved::is_root() const {
    return is_valid() && window->resolved_scope_id().is_root();
}

bool FocusScope::Resolved::is_subwindow() const {
    return is_valid() && !window->resolved_scope_id().is_root();
}

bool FocusScope::Resolved::is_read_only() const {
    return window && window->read_only;
}
