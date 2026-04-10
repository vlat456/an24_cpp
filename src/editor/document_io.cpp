#include "document.h"

#include "blueprint_view_hydration.h"
#include "json_parser/json_parser.h"
#include "visual/persist.h"
#include "visual/scene_mutations.h"

#include <spdlog/spdlog.h>

bool Document::save(const std::string& path) {
    if (!type_registry_) {
        spdlog::error("[persist] TypeRegistry is not configured on Document::save");
        return false;
    }

    std::string validation_error;
    if (!validate_blueprint_for_persist(model_.current(), interner_, arena_, *type_registry_, &validation_error)) {
        spdlog::error("[persist] Refusing to save invalid blueprint '{}': {}", path, validation_error);
        return false;
    }

    if (!save_blueprint_to_file(model_.current(), interner_, arena_, *type_registry_, path.c_str())) {
        return false;
    }

    filepath_ = path;
    auto pos = path.find_last_of("/\\");
    display_name_ = (pos != std::string::npos) ? path.substr(pos + 1) : path;
    model_.mark_saved();
    return true;
}

bool Document::load(const std::string& path) {
    if (!type_registry_) {
        spdlog::error("[persist] TypeRegistry is not configured on Document::load");
        return false;
    }

    auto bp = load_blueprint_from_file_validated(path.c_str(), interner_, arena_, *type_registry_);
    if (!bp.has_value()) {
        return false;
    }

    window_manager_.close_all();
    root().input.cancel_gesture();

    if (simulation_running_) {
        simulation_.stop();
        simulation_running_ = false;
    }

    bp = editor::hydrate_runtime_node_view_data(std::move(*bp), interner_, *type_registry_);

    {
        bp2::EditorModel fresh(std::move(*bp));
        model_ = std::move(fresh);
        sync_next_wire_id();
        model_.mark_saved();
    }

    viewport() = Viewport{};

    visual::mutations::rebuild(scene(), model_.current(), interner_, arena_, root().resolved_scope_id().sim_scope_prefix());

    filepath_ = path;
    auto pos = path.find_last_of("/\\");
    display_name_ = (pos != std::string::npos) ? path.substr(pos + 1) : path;
    return true;
}

void Document::sync_next_wire_id() {
    int max_seen = -1;
    for (const auto& w : model_.current().wires()) {
        std::string_view wid = interner_.resolve(w.id);
        if (wid.size() <= 5 || wid.substr(0, 5) != "wire_") {
            continue;
        }
        int n = 0;
        bool ok = true;
        for (size_t i = 5; i < wid.size(); ++i) {
            char c = wid[i];
            if (c < '0' || c > '9') {
                ok = false;
                break;
            }
            n = n * 10 + (c - '0');
        }
        if (ok && n > max_seen) {
            max_seen = n;
        }
    }
    model_.next_wire_id_ = max_seen + 1;
}
