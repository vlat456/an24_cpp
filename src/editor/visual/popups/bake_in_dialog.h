#pragma once

#include "editor/window_system.h"
#include "editor/input/editing_host.h"
#include "editor/visual/persist.h"
#include "blueprint_v2/library/blueprint_library.h"
#include "blueprint_v2/library/type_def_to_blueprint.h"
#include "core/model/component_spec.h"
#include <imgui.h>
#include <spdlog/spdlog.h>

namespace {

inline std::unique_ptr<EditingHost> create_bake_host_for_scope(Document& doc,
                                                               const WindowScopeId& scope_id) {
    if (scope_id.is_external()) {
        return nullptr;
    }
    if (scope_id.is_root()) {
        return create_editor_model_host(doc.model());
    }

    // scope_id.path() already returns InternedId vector - use directly
    return create_pathful_embedded_host(doc.model(), std::vector<ui::InternedId>(scope_id.path().begin(), scope_id.path().end()));
}

inline bp2::BlueprintLibrary build_bake_library(Document& doc) {
    bp2::BlueprintLibrary library;
    const ComponentRegistry* registry = doc.type_registry();
    if (!registry) {
        return library;
    }

    for (const auto& [classname, spec] : registry->types) {
        if (!is_composite(spec)) {
            continue;
        }
        try {
            library.add(doc.interner().intern(classname),
                        bp2::blueprint_from_type_definition(spec, doc.interner(), *registry));
        } catch (const std::exception& e) {
            spdlog::warn("[bake-in] failed to build library blueprint '{}': {}", classname, e.what());
        }
    }

    return library;
}

} // namespace


/// Bake-in confirmation dialog for sub-blueprint embedding
class BakeInDialog {
public:
    void render(WindowSystem& ws) {
        if (ws.pendingBakeIn.show_confirmation) {
            ImGui::OpenPopup("Bake In Confirmation");
            ws.pendingBakeIn.show_confirmation = false;
        }

        if (ImGui::BeginPopupModal("Bake In Confirmation", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Are you sure you want to bake in this sub-blueprint?");
            ImGui::Text("This will embed all nodes from the library file directly into this document.");
            ImGui::Separator();

            if (ImGui::Button("Bake In")) {
                Document* bake_doc = ws.pendingBakeIn.document_id
                    ? ws.findDocumentById(*ws.pendingBakeIn.document_id)
                    : nullptr;
                if (!bake_doc) {
                    ws.pendingBakeIn.reset();
                    ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                    return;
                }

                std::unique_ptr<EditingHost> host =
                    create_bake_host_for_scope(*bake_doc, ws.pendingBakeIn.scope_id);
                if (!host) {
                    spdlog::warn("[bake-in] rejected for unavailable or read-only scope '{}'",
                                 editor::instance_path_to_scope_string(bake_doc->interner(), ws.pendingBakeIn.scope_id.path()));
                    ws.pendingBakeIn.reset();
                    ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                    return;
                }

                const ui::InternedId node_iid = bake_doc->interner().lookup(ws.pendingBakeIn.node_id.str());
                bool ok = false;
                if (!node_iid.empty()) {
                    bp2::BlueprintLibrary library = build_bake_library(*bake_doc);
                    ok = host->bake_blueprint_instance(node_iid, library);
                }

                if (!ok) {
                    spdlog::warn("[bake-in] bake_blueprint_instance failed for '{}' in scope '{}'",
                                 ws.pendingBakeIn.node_id.str(),
                                 editor::instance_path_to_scope_string(bake_doc->interner(), ws.pendingBakeIn.scope_id.path()));
                } else {
                    bake_doc->rebuildAllWindows();
                }

                ws.pendingBakeIn.reset();
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel")) {
                ws.pendingBakeIn.reset();
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }
};
