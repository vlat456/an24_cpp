// examples/an24_editor.cpp

/// Настроить ImGui Docking layout для editor'а
static void SetupEditorLayout() {
    static bool layout_initialized = false;
    if (!layout_initialized) {
        layout_initialized = true;

        ImGuiID dockspace_id = ImGui::GetID("EditorDockSpace");

        // Clear existing layout
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGuiID center = dockspace_id;

        // Split into 3 columns: Left | Center | Right
        ImGuiID left_id, right_id;
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Left,  0.20f, &left_id,  &center);
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.25f, &right_id, &center);

        // Split center into Top | Bottom
        ImGuiID bottom_id;
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Down,  0.20f, &bottom_id, &center);

        // Dock windows to nodes
        ImGui::DockBuilderDockWindow("Component Tree", left_id);
        ImGui::DockBuilderDockWindow("Canvas", center_id);
        ImGui::DockBuilderDockWindow("Properties", right_id);
        ImGui::DockBuilderDockWindow("Simulation Log", bottom_id);

        ImGui::DockBuilderFinish(dockspace_id);
    }
}

int main(int argc, char** argv) {
    // ... SDL/GL/ImGui setup ...

    while (running) {
        // ... events ...

        ImGui::NewFrame();

        // ===== DOCKSPACE =====
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);

        ImGuiWindowFlags dockspace_flags = ImGuiWindowFlags_MenuBar |
                                           ImGuiWindowFlags_NoDocking;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

        ImGui::Begin("EditorDockSpace", nullptr, dockspace_flags);
        ImGui::PopStyleVar(3);

        SetupEditorLayout();  // <-- Настройка layout

        ImGui::End();

        // ===== MENU BAR =====
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New", "Ctrl+N")) app.new_circuit();
                if (ImGui::MenuItem("Open...", "Ctrl+O")) { /* ... */ }
                if (ImGui::MenuItem("Save", "Ctrl+S")) { /* ... */ }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit", "Alt+F4")) running = false;
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("View")) {
                if (ImGui::MenuItem("Reset Layout")) {
                    layout_initialized = false;  // Force rebuild
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Component Tree", "Ctrl+T", show_tree))
                    show_tree = !show_tree;
                if (ImGui::MenuItem("Simulation Log", "Ctrl+L", show_log))
                    show_log = !show_log;
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Window")) {
                ImGui::MenuItem("Component Tree", nullptr, show_tree);
                ImGui::MenuItem("Simulation Log", nullptr, show_log);
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }

        // ===== RENDER DOCKED WINDOWS =====

        // Component Tree (LEFT PANEL)
        if (show_tree) {
            ImGui::Begin("Component Tree", &show_tree);
            app.RenderComponentTree();
            ImGui::End();
        }

        // Canvas (CENTER - MAIN AREA)
        RenderCanvas(app);

        // Properties (RIGHT PANEL - conditional)
        app.properties_window.render();

        // Simulation Log (BOTTOM PANEL)
        if (show_log) {
            ImGui::Begin("Simulation Log", &show_log);
            RenderSimLog(app);
            ImGui::End();
        }

        // ===== RENDER =====
        ImGui::Render();
        // ... GL rendering ...
    }

    // ... cleanup ...
}
