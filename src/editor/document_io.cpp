#include "document.h"

#include "json_parser/json_parser.h"
#include "visual/persist.h"
#include "visual/scene_mutations.h"

#include <spdlog/spdlog.h>

bool Document::save(const std::string& path) {
    const auto& vp = viewport();
    auto updated = model_.current().with_viewport(vp.pan.x, vp.pan.y, vp.zoom, vp.grid_step);
    model_.replace_current(std::move(updated));

    TypeRegistry parser_registry = load_type_registry("library/");
    std::string validation_error;
    if (!validate_blueprint_for_persist(model_.current(), interner_, arena_, parser_registry, &validation_error)) {
        spdlog::error("[persist] Refusing to save invalid blueprint '{}': {}", path, validation_error);
        return false;
    }

    if (!save_blueprint_to_file(model_.current(), interner_, arena_, path.c_str())) {
        return false;
    }

    filepath_ = path;
    auto pos = path.find_last_of("/\\");
    display_name_ = (pos != std::string::npos) ? path.substr(pos + 1) : path;
    model_.mark_saved();
    return true;
}

bool Document::load(const std::string& path) {
    TypeRegistry parser_registry = load_type_registry("library/");
    auto bp = load_blueprint_from_file_validated(path.c_str(), interner_, arena_, parser_registry);
    if (!bp.has_value()) {
        return false;
    }

    window_manager_.close_all();
    root().input.cancel_gesture();

    if (simulation_running_) {
        simulation_.stop();
        simulation_running_ = false;
    }

    model_.replace_current(std::move(*bp));

    {
        bp2::EditorModel fresh(model_.current());
        model_ = std::move(fresh);
        sync_next_wire_id();
        model_.mark_saved();
    }

    auto& vp = viewport();
    vp.pan.x = model_.current().pan_x();
    vp.pan.y = model_.current().pan_y();
    vp.zoom = model_.current().zoom();
    vp.grid_step = model_.current().grid_step();
    vp.clamp_zoom();

    visual::mutations::rebuild(scene(), model_.current(), interner_, arena_, root().group_id);

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
