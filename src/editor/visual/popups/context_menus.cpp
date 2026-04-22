#include "context_menus.h"
#include "editor/input/input_types.h"
#include "editor/subwindow_open_target.h"
#include <imgui.h>
#include <cstring>


void ContextMenus::renderAddComponent(WindowSystem& ws) {
    // OpenPopup is a one-shot trigger; BeginPopup must run every frame
    if (ws.contextMenu.show) {
        ImGui::OpenPopup("AddComponent");
        ws.contextMenu.show = false;
    }
    
    if (!ImGui::BeginPopup("AddComponent")) return;

    Document* owner_doc = ws.contextMenu.source_document_id
        ? ws.findDocumentById(*ws.contextMenu.source_document_id)
        : nullptr;
    if (!owner_doc) {
        ws.contextMenu.source_document_id.reset();
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Add Component");
    ImGui::Separator();
    
    auto menu_tree = ws.typeRegistry().build_menu_tree();
    std::function<void(const MenuTree&)> render_menu;
    render_menu = [&](const MenuTree& tree) {
        for (const auto& [folder, subtree] : tree.children) {
            if (ImGui::BeginMenu(folder.c_str())) {
                render_menu(subtree);
                ImGui::EndMenu();
            }
        }
        for (const auto& classname : tree.entries) {
            auto lbl_it = tree.labels.find(classname);
            const std::string& label = (lbl_it != tree.labels.end()) ? lbl_it->second : classname;
            if (ImGui::MenuItem(label.c_str())) {
                owner_doc->addComponent(classname, ws.contextMenu.position, ws.contextMenu.scope_id, ws.typeRegistry());
            }
        }
    };
    render_menu(menu_tree);
    
    ImGui::EndPopup();
}

void ContextMenus::renderNodeContext(WindowSystem& ws) {
    // OpenPopup is a one-shot trigger; BeginPopup must run every frame
    if (ws.nodeContextMenu.show) {
        ImGui::OpenPopup("NodeContextMenu");
        ws.nodeContextMenu.show = false;
    }
    
    if (!ImGui::BeginPopup("NodeContextMenu")) return;

    Document* doc = ws.nodeContextMenu.source_document_id
        ? ws.findDocumentById(*ws.nodeContextMenu.source_document_id)
        : nullptr;
    if (!doc) {
        ws.nodeContextMenu.source_document_id.reset();
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    // Resolve the node from the correct scoped blueprint
    const bp2::Blueprint::Node* node_ptr = nullptr;
    if (doc) {
        node_ptr = doc->find_node_in_scope(ws.nodeContextMenu.scope_id, ws.nodeContextMenu.node_id);
    }

    if (doc && node_ptr) {
        const bp2::Blueprint::Node& node = *node_ptr;
        ImGui::Text("Node: %s", node.view.name.c_str());
        ImGui::Separator();
        
        bool is_read_only = false;
        if (!ws.nodeContextMenu.scope_id.is_root()) {
            BlueprintWindow* win = doc->windowManager().find(ws.nodeContextMenu.scope_id);
            is_read_only = win && win->read_only;
        }
        
        if (!is_read_only) {
            if (ImGui::MenuItem("Properties...")) {
                ws.openPropertiesForNode(ws.nodeContextMenu.node_id, ws.nodeContextMenu.scope_id, *doc);
            }
        }
        if (!is_read_only) {
            if (ImGui::MenuItem("Set Color...")) {
                ws.openColorPickerForNode(ws.nodeContextMenu.node_id, ws.nodeContextMenu.scope_id, *doc);
            }
            const CanvasInput* source_input = &doc->input();
            if (!ws.nodeContextMenu.scope_id.is_root()) {
                if (BlueprintWindow* win = doc->windowManager().find(ws.nodeContextMenu.scope_id)) {
                    source_input = &win->input;
                }
            }
            const auto& selected = source_input->selected_node_ids();
            const bool can_extract = selected.size() >= 2;
            if (ImGui::MenuItem("Extract to Blueprint...", nullptr, false, can_extract)) {
                ws.pendingExtract.show_dialog = true;
                ws.pendingExtract.document_id = doc->id();
                ws.pendingExtract.scope_id = ws.nodeContextMenu.scope_id;
                ws.pendingExtract.selected_node_ids = selected;
                ws.pendingExtract.has_preview = false;
                ws.pendingExtract.preview = {};
                ws.pendingExtract.preview_error.clear();
                ws.pendingExtract.preview_name.clear();
                ws.pendingExtract.allow_nonembedded_descendant_refs = false;
                ws.pendingExtract.preview_allow_nonembedded_descendant_refs = false;

                 std::string suggested = "extracted_blueprint_1";
                 int idx = 1;
                 while (idx < 100000) {
                     std::string candidate = "extracted_blueprint_" + std::to_string(idx);
                     bool used = false;
                     ui::InternedId cid = doc->interner().lookup(candidate);
                     if (!cid.empty()) {
                         for (const auto& nn : doc->blueprint().nodes()) {
                             if (nn.is_blueprint_instance() && nn.semantic.type == cid) {
                                 used = true;
                                 break;
                             }
                         }
                     }
                     if (!used) {
                         for (const auto& node_it : doc->blueprint().nodes()) {
                             if (node_it.view.name == candidate) {
                                 used = true;
                                 break;
                             }
                         }
                     }
                     if (!used) {
                         suggested = std::move(candidate);
                         break;
                     }
                     ++idx;
                 }
                std::memset(ws.pendingExtract.name_buf, 0, sizeof(ws.pendingExtract.name_buf));
                std::strncpy(ws.pendingExtract.name_buf, suggested.c_str(), sizeof(ws.pendingExtract.name_buf) - 1);

                // Preview is computed lazily by ExtractToBlueprintDialog
                // on first render frame (avoids duplicate computation).
            }
            if (ImGui::MenuItem("Delete")) {
                CanvasInput* source_input = &doc->input();
                if (!ws.nodeContextMenu.scope_id.is_root()) {
                    if (BlueprintWindow* win = doc->windowManager().find(ws.nodeContextMenu.scope_id)) {
                        source_input = &win->input;
                    }
                }
                auto action = doc->applyInputResult(source_input->on_key(Key::Delete), ws.nodeContextMenu.scope_id);
                ws.handleInputAction(action, *doc);
            }
        }
        
        if (node.is_blueprint_instance() && ImGui::MenuItem("Open in editor")) {
            const bp2::LibraryIndex* lib_idx = doc->library_index();
            if (!lib_idx) {
                return;
            }
            std::string node_id_str(doc->interner().resolve(node.semantic.id));
            const auto result = editor::resolve_subwindow_open_target(
                doc->blueprint(), doc->interner(), *lib_idx, node_id_str);
            const auto& target = result.target;
            if (target.kind == editor::SubWindowOpenTargetKind::ExternalReference) {
                ws.openDocument(target.path);
            } else {
                doc->openSubWindow(ws.nodeContextMenu.scope_id, node_id_str);
            }
        }
        
         // Check for a blueprint instance (reference) that can be baked in.
         // The right-clicked node itself may be a reference that can be embedded.
         const bp2::Blueprint::Node* sbi_node = node_ptr;
         if (!is_read_only && sbi_node && sbi_node->is_blueprint_instance() && sbi_node->has_referenced_blueprint()) {
             if (ImGui::MenuItem("Bake In (Embed)")) {
                  ws.pendingBakeIn.show_confirmation = true;
                  ws.pendingBakeIn.scope_id = ws.nodeContextMenu.scope_id;
                  ws.pendingBakeIn.node_id = ws.nodeContextMenu.node_id;
                  ws.pendingBakeIn.document_id = ws.nodeContextMenu.source_document_id
                      ? ws.nodeContextMenu.source_document_id
                      : (doc ? std::optional<editor::DocumentId>(doc->id()) : std::nullopt);
             }
              ImGui::Separator();
              if (ImGui::MenuItem("Open in editor")) {
                  std::string bp_id_str(doc->interner().resolve(sbi_node->blueprint_instance().source.blueprint_id()));
                  auto lib_path_opt = ws.libraryIndex().resolve(bp_id_str);
                  if (lib_path_opt) {
                      ws.openDocument(lib_path_opt.value());
                  }
              }
         }
    }
    
    ImGui::EndPopup();
}
