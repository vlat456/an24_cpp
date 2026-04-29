#pragma once

#include "document.h"
#include "editor_settings.h"
#include "editor/rendering_resources.h"
#include "editor/transient_ui_manager.h"
#include "focus_scope.h"
#include "visual/inspector/inspector.h"
#include "editor/input/editing_host.h"
#include "window/properties_window.h"
#include "window/script_editor_window.h"
#include "window/window_scope_id.h"
#include "core/model/component_registry.h"
#include "blueprint_v2/library/library_index.h"
#include "commands/extract_blueprint.h"
#include "pi_zn_tuner.h"
#include "oscilloscope.h"
#include <cstring>
#include <memory>
#include <optional>
#include <vector>
#include <string>

/// Manages all open documents and global panels.
/// Replaces EditorApp as the top-level controller.
class WindowSystem {
public:
    WindowSystem();

    // ── Document lifecycle ──

    Document& createDocument();
    Document* openDocument(const std::string& path);
    bool closeDocument(Document& doc);
    bool closeAllDocuments();

    // ── Active document ──

    Document* activeDocument() { return active_document_; }
    const Document* activeDocument() const { return active_document_; }
    void setActiveDocument(Document* doc);

    // ── Tab focus (one-shot programmatic selection) ──

    Document* pendingTabFocus() const { return pending_tab_focus_; }
    void consumeTabFocus() { pending_tab_focus_ = nullptr; }
    void setPendingTabFocus(Document* doc) { pending_tab_focus_ = doc; }

    // ── Document access ──

    const std::vector<std::unique_ptr<Document>>& documents() const { return documents_; }
    size_t documentCount() const { return documents_.size(); }
    Document* findDocumentByPath(const std::string& path);
    Document* findDocumentById(const editor::DocumentId& id);
    const Document* findDocumentById(const editor::DocumentId& id) const;

    // ── Focus scope (menu context) ──

    FocusScope focus_scope;

    void resetFocusToRoot();
    FocusScope::Resolved resolve_focus();

    // ── Global panels ──

    Inspector& inspector() { return inspector_; }
    PropertiesWindow& propertiesWindow() { return properties_window_; }
    ScriptEditorWindow& scriptEditorWindow() { return script_editor_window_; }
    ComponentRegistry& typeRegistry() { return type_registry_; }
    const bp2::LibraryIndex& libraryIndex() const { return library_index_; }
    editor::RenderingResources& renderingResources() { return rendering_resources_; }

    // ── Transient UI state (lifecycle managed by TransientUIManager) ──

    struct ContextMenuState {
        bool show = false;
        Pt position;
        WindowScopeId scope_id = WindowScopeId::root();
        std::optional<editor::DocumentId> source_document_id;

        bool is_open() const;
        void close();
        bool owns_document(const editor::DocumentId& id) const;
        bool still_valid(WindowSystem& ws) const;
    } contextMenu;

    struct NodeContextMenuState {
        bool show = false;
        core::InternedId node_id;
        WindowScopeId scope_id = WindowScopeId::root();
        std::optional<editor::DocumentId> source_document_id;

        bool is_open() const;
        void close();
        bool owns_document(const editor::DocumentId& id) const;
        bool still_valid(WindowSystem& ws) const;
    } nodeContextMenu;

    struct ColorPickerState {
        bool show = false;
        core::InternedId node_id;
        WindowScopeId scope_id = WindowScopeId::root();
        std::optional<editor::DocumentId> source_document_id;
        float rgba[4] = {0.5f, 0.5f, 0.5f, 1.0f};

        bool is_open() const;
        void close();
        bool owns_document(const editor::DocumentId& id) const;
        bool still_valid(WindowSystem& ws) const;
    } colorPicker;

    struct PendingBakeIn {
        bool show_confirmation = false;
        std::optional<editor::DocumentId> document_id;
        WindowScopeId scope_id = WindowScopeId::root();
        core::InternedId node_id;

        bool is_open() const;
        void close();
        bool owns_document(const editor::DocumentId& id) const;
        bool still_valid(WindowSystem& ws) const;

    } pendingBakeIn;

    struct SetNameState {
        bool show = false;
        std::optional<editor::DocumentId> document_id;
        bool save_after = false;
        char buf[128] = {};

        bool is_open() const;
        void close();
        bool owns_document(const editor::DocumentId& id) const;
        bool still_valid(WindowSystem& ws) const;
    } setName;

    struct PendingExtractToBlueprint {
        bool show_dialog = false;
        std::optional<editor::DocumentId> document_id;
        WindowScopeId scope_id = WindowScopeId::root();
        std::vector<core::InternedId> selected_node_ids;
        char name_buf[128] = {};
        bool has_preview = false;
        editor::commands::ExtractToBlueprintPreview preview;
        std::string preview_error;
        std::string preview_name;
        bool allow_nonembedded_descendant_refs = false;
        bool preview_allow_nonembedded_descendant_refs = false;

        bool is_open() const;
        void close();
        bool owns_document(const editor::DocumentId& id) const;
        bool still_valid(WindowSystem& ws) const;

    } pendingExtract;

    struct ZNTuneState {
        bool show_result_popup = false;
        bool last_ok = false;
        bool last_was_preview = false;
        char pi_node[128] = "pi_1";
        char feedback_signal[128] = "bus_1.v";
        float cfg_dt_sec = 1.0f / 60.0f;
        float cfg_run_time_sec = 16.0f;
        float cfg_settle_time_sec = 3.0f;
        float cfg_kp_lo = 0.01f;
        float cfg_kp_hi = 80.0f;
        int cfg_max_expand = 10;
        int cfg_binary_iters = 14;
        int cfg_min_peaks = 4;
        float Ku = 0.0f;
        float Tu = 0.0f;
        float Kp = 0.0f;
        float Ki = 0.0f;
        char error[256] = {};
        ZNTuneConfig last_cfg{};

        bool is_open() const;
        void close();
        bool owns_document(const editor::DocumentId& id) const;
        bool still_valid(WindowSystem& ws) const;
    } znTune;

    bool showInspector = true;
    bool showOscilloscope = true;
    bool showDebugLayoutBounds = false;
    bool showDebugPaintBounds = false;
    EditorSettings settings;
    OscilloscopeModel oscilloscope;

    struct InlineValueEditorState {
        bool open = false;
        std::optional<editor::DocumentId> document_id;
        core::InternedId node_id;
        WindowScopeId scope_id = WindowScopeId::root();
        std::string buffer;
        std::string error;
        Pt anchor_screen;
        bool has_anchor = false;

        std::unique_ptr<EditingHost> cached_host;

        bool is_open() const;
        void close();
        bool owns_document(const editor::DocumentId& id) const;
        bool still_valid(WindowSystem& ws) const;
    } inlineValueEditor;

    // ── Utility ──

    void removeClosedDocuments();

    void openPropertiesForNode(core::InternedId node_id, const WindowScopeId& scope_id, Document& doc);

    void openScriptEditorForNode(core::InternedId node_id, const WindowScopeId& scope_id, Document& doc);

    void openColorPickerForNode(core::InternedId node_id, const WindowScopeId& scope_id, Document& doc);

    void openInlineValueEditorForNode(core::InternedId node_id, const WindowScopeId& scope_id, Document& doc,
                                      const ui::Pt* anchor_screen = nullptr);

    void handleInputAction(const Document::InputResultAction& action, Document& doc);

    void reconcile_owner_bound_ui();

private:
    void register_transient_ui();

    std::vector<std::unique_ptr<Document>> documents_;
    Document* active_document_ = nullptr;
    Document* pending_tab_focus_ = nullptr;
    ComponentRegistry type_registry_;
    bp2::LibraryIndex library_index_;
    Inspector inspector_;
    PropertiesWindow properties_window_;
    ScriptEditorWindow script_editor_window_;
    editor::RenderingResources rendering_resources_;
    TransientUIManager transient_ui_;
};
