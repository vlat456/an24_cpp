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
            if (ImGui::MenuItem(classname.c_str())) {
                Document* doc = ws.contextMenu.source_doc ? ws.contextMenu.source_doc : ws.activeDocument();
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
    
    Document* doc = ws.nodeContextMenu.source_doc ? ws.nodeContextMenu.source_doc : ws.activeDocument();
    Node* node_ptr = doc ? doc->blueprint().find_node(ws.nodeContextMenu.node_id.c_str()) : nullptr;
    if (doc && node_ptr) {
        Node& node = *node_ptr;
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
            doc->openSubWindow(node.id);
        }
        
        const std::string& sbi_id = !ws.nodeContextMenu.group_id.empty()
            ? ws.nodeContextMenu.group_id : node.id;
        auto* sb = doc->blueprint().find_sub_blueprint_instance(sbi_id);
        if (sb && !sb->baked_in) {
            if (ImGui::MenuItem("Bake In (Embed)")) {
                ws.pendingBakeIn.show_confirmation = true;
                ws.pendingBakeIn.sub_blueprint_id = sbi_id;
                ws.pendingBakeIn.doc = ws.nodeContextMenu.source_doc ? ws.nodeContextMenu.source_doc : doc;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Edit Original")) {
                std::string lib_path = "library/" + sb->blueprint_path + ".json";
                ws.openDocument(lib_path);
            }
        }
    }
    
    ImGui::EndPopup();
}

