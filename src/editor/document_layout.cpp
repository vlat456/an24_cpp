#include "document.h"

#include "blueprint_v2/editor_model/editor_model.h"
#include "visual/node/visual_node.h"
#include "visual/scene_mutations.h"
#include "visual/snap.h"

bool Document::apply_normalized_node_sizes(bool preserve_manual,
                                           bool push_checkpoint,
                                           bool rebuild_windows) {
    bool changed = false;

    bp2::Blueprint updated = model_.current();
    visual::Scene probe_scene;
    ComponentRegistry empty_reg;
    const ComponentRegistry& reg = type_registry_ ? *type_registry_ : empty_reg;
    visual::mutations::rebuild(probe_scene, updated, interner_, arena_, root().resolved_scope_id().sim_scope_prefix(), reg);

    for (const auto& node : model_.current().nodes()) {
        if (preserve_manual && node.layout.manual_size) {
            continue;
        }

        auto* widget = dynamic_cast<visual::NodeWidget*>(probe_scene.find(std::string(interner_.resolve(node.semantic.id))));
        if (!widget) {
            continue;
        }

        Pt minimum = widget->minimumNodeSize();
        Pt target = minimum;
        bool width_changed = false;
        bool height_changed = false;

        if (!push_checkpoint) {
            target = widget->size();
            const float current_width = node.layout.width.value_or(target.x);
            const float current_height = node.layout.height.value_or(target.y);
            width_changed = current_width < target.x;
            height_changed = current_height < target.y;
        } else {
            const float current_width = node.layout.width.value_or(minimum.x);
            const float current_height = node.layout.height.value_or(minimum.y);
            width_changed = current_width != minimum.x;
            height_changed = current_height != minimum.y;
        }

        if (!width_changed && !height_changed) {
            continue;
        }

        changed = true;
        bp2::Blueprint::Node resized = node;
        resized.layout.width = push_checkpoint ? minimum.x : target.x;
        resized.layout.height = push_checkpoint ? minimum.y : target.y;
        resized.layout.manual_size = false;
        updated = bp2::replace_node_preserve_order(updated, std::move(resized));
    }

    if (!changed) {
        return false;
    }

    if (push_checkpoint) {
        model_.push_checkpoint();
    }
    model_.replace_current(std::move(updated));
    if (rebuild_windows) {
        rebuildAllWindows();
    }
    return true;
}

bool Document::normalizeNodeSizesToFit(bool preserve_manual) {
    return apply_normalized_node_sizes(
        preserve_manual,
        true,
        true);
}
