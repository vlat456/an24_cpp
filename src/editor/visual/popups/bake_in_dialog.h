#pragma once

#include "editor/window_system.h"
#include "blueprint_v2/registry/type_registry.h"
#include <imgui.h>
#include <spdlog/spdlog.h>


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
                Document* bake_doc = ws.findDocumentById(ws.pendingBakeIn.doc_id);
                if (bake_doc) {
                    ui::InternedId nested_iid =
                        bake_doc->interner().lookup(ws.pendingBakeIn.sub_blueprint_id);
                    if (!nested_iid.empty()) {
                        // NOTE: bake_nested() requires a populated bp2::TypeRegistry to look
                        // up the library blueprint by ID. If the registry is empty this call
                        // returns false for reference-type nested blueprints. Embedded
                        // (inline_def) nesteds are unaffected.
                        bp2::TypeRegistry bp2_registry;
                        bool ok = bake_doc->model().bake_nested(
                            nested_iid, bp2_registry, bake_doc->interner());
                        if (!ok) {
                            spdlog::warn("[bake-in] bake_nested failed for '{}' – "
                                         "bp2::TypeRegistry not populated",
                                         ws.pendingBakeIn.sub_blueprint_id);
                        }
                    }
                    bake_doc->rebuildAllWindows();
                }
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel")) {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }
};
