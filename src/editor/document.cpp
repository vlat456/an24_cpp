#include "document.h"
#include "commands/extract_blueprint.h"
#include "debug.h"
#include <spdlog/spdlog.h>

int Document::next_id_ = 1;

Document::Document() {
    id_ = "doc_" + std::to_string(next_id_++);
}

std::string Document::title() const {
    std::string base;
    if (!model_.current().name().empty()) {
        base = model_.current().name();
    } else if (!model_.current().display_name().empty()) {
        base = model_.current().display_name();
    } else {
        base = display_name_;
    }
    if (model_.is_dirty()) {
        base += "*";
    }
    if (window_manager_.root().read_only) {
        base += " [Read Only]";
    }
    return base;
}

// ============================================================================
// Private helpers
// ============================================================================

// save/load/sync_next_wire_id moved to document_io.cpp
// simulation/content/overrides methods moved to document_simulation.cpp
// window opening methods moved to document_windows.cpp

// addComponent/addBlueprint moved to document_components.cpp

bool Document::extractToBlueprint(const std::vector<ui::InternedId>& selected_node_ids,
                                  const std::string& blueprint_name,
                                  const std::string& scope_id,
                                  std::string* error_out,
                                  bool allow_nonembedded_descendant_refs) {
    if (!type_registry_) {
        if (error_out) {
            *error_out = "TypeRegistry is not configured on Document::extractToBlueprint";
        }
        return false;
    }

    auto updated = editor::commands::build_extracted_blueprint_atomic(
        model_.current(), selected_node_ids, blueprint_name, scope_id,
        interner_, arena_, *type_registry_, error_out, allow_nonembedded_descendant_refs);
    if (!updated) {
        return false;
    }

    model_.push_checkpoint();
    model_.replace_current(std::move(*updated));

    rebuildAllWindows();
    return true;
}

// ============================================================================
// Sub-windows
// ============================================================================

// openSubWindow moved to document_windows.cpp

// applyInputResult moved to document_input.cpp
// performUndo/performRedo moved to document_history.cpp
