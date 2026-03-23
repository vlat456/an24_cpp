#include "context_menus.h"
#include "editor/input/input_types.h"
#include <imgui.h>


void ContextMenus::renderAddComponent(WindowSystem& ws) {
    // OpenPopup is a one-shot trigger; BeginPopup must run every frame
    if (ws.contextMenu.show) {
        ImGui::OpenPopup("AddComponent");
        ws.contextMenu.show = false;
    }
    
    if (!ImGui::BeginPopup("AddComponent")) return;
    
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
                Document* doc = ws.findDocumentById(ws.contextMenu.source_doc_id);
                if (!doc) doc = ws.activeDocument();
                if (doc) {
                    doc->addComponent(classname, ws.contextMenu.position, ws.contextMenu.group_id, ws.typeRegistry());
                }
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
    
    Document* doc = ws.findDocumentById(ws.nodeContextMenu.source_doc_id);
    if (!doc) doc = ws.activeDocument();

    // Resolve the node
    const bp2::Blueprint::Node* node_ptr = nullptr;
    if (doc) {
        ui::InternedId node_iid = doc->interner().lookup(ws.nodeContextMenu.node_id);
        if (!node_iid.empty()) {
            node_ptr = doc->blueprint().find_node(node_iid);
        }
    }

    if (doc && node_ptr) {
        const bp2::Blueprint::Node& node = *node_ptr;
        ImGui::Text("Node: %s", node.name.c_str());
        ImGui::Separator();
        
        bool is_read_only = false;
        if (!ws.nodeContextMenu.group_id.empty()) {
            BlueprintWindow* win = doc->windowManager().find(ws.nodeContextMenu.group_id);
            is_read_only = win && win->read_only;
        }
        
        if (ImGui::MenuItem("Properties...")) {
            ws.openPropertiesForNode(ws.nodeContextMenu.node_id, *doc);
        }
        if (!is_read_only) {
            if (ImGui::MenuItem("Set Color...")) {
                ws.openColorPickerForNode(ws.nodeContextMenu.node_id, ws.nodeContextMenu.group_id, *doc);
            }
            if (ImGui::MenuItem("Delete")) {
                auto action = doc->applyInputResult(doc->input().on_key(Key::Delete), ws.nodeContextMenu.group_id);
                ws.handleInputAction(action, *doc);
            }
        }
        
        if (node.expandable && ImGui::MenuItem("Open in New Window")) {
            std::string node_id_str(doc->interner().resolve(node.id));
            doc->openSubWindow(node_id_str);
        }
        
        // Check for a nested (sub-blueprint) reference that can be baked in
        const std::string& sbi_id = !ws.nodeContextMenu.group_id.empty()
            ? ws.nodeContextMenu.group_id : ws.nodeContextMenu.node_id;
        ui::InternedId sbi_iid = doc->interner().lookup(sbi_id);
        const bp2::Blueprint::Nested* nested = sbi_iid.empty()
            ? nullptr : doc->blueprint().find_nested(sbi_iid);
        if (nested && !nested->embedded) {
            if (ImGui::MenuItem("Bake In (Embed)")) {
                ws.pendingBakeIn.show_confirmation = true;
                ws.pendingBakeIn.sub_blueprint_id = sbi_id;
                ws.pendingBakeIn.doc_id = !ws.nodeContextMenu.source_doc_id.empty()
                    ? ws.nodeContextMenu.source_doc_id : (doc ? doc->id() : std::string{});
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Edit Original")) {
                // Resolve blueprint path from the nested's inline_def or blueprint_id
                std::string bp_id_str(doc->interner().resolve(nested->blueprint_id));
                std::string lib_path = "library/" + bp_id_str + ".json";
                ws.openDocument(lib_path);
            }
        }
    }
    
    ImGui::EndPopup();
}
